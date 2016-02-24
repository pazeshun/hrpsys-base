// -*- coding:utf-8-unix; mode: c++; indent-tabs-mode: nil; c-basic-offset: 2; -*-
#ifndef EKFILTER_H
#define EKFILTER_H

#include <hrpUtil/EigenTypes.h>
#include <iostream>
#include "util/Hrpsys.h"

namespace hrp{
  typedef Eigen::Matrix<double, 7, 7> Matrix77;
  typedef Eigen::Matrix<double, 7, 1> Vector7;
};

class EKFilter {
public:
  EKFilter()
    : P(hrp::Matrix77::Identity() * 0.1),
      Q(Eigen::Matrix3d::Identity() * 0.001),
      R(Eigen::Matrix3d::Identity() * 0.03),
      g_vec(Eigen::Vector3d(0.0, 0.0, 9.80665))
  {
    x << 1, 0, 0, 0, 0, 0, 0;
  }

  hrp::Vector7 getx() const { return x; }

  void calcOmega(Eigen::Matrix4d& omega, const Eigen::Vector3d& w) const {
    /* \dot{q} = \frac{1}{2} omega q */
    omega <<
      0, -w[0], -w[1], -w[2],
      w[0],     0,  w[2], -w[1],
      w[1], -w[2],     0,  w[0],
      w[2],  w[1], -w[0],     0;
  }

  void calcPredictedState(hrp::Vector7& _x_a_priori,
                          const Eigen::Vector4d& q,
                          const Eigen::Vector3d& gyro,
                          const Eigen::Vector3d& drift) const {
    /* x_a_priori = f(x, u) */
    Eigen::Vector4d q_a_priori;
    Eigen::Vector3d gyro_compensated = gyro - drift;
    Eigen::Matrix4d omega;
    calcOmega(omega, gyro_compensated);
    q_a_priori = q + dt / 2 * omega * q;
    _x_a_priori.head<4>() = q_a_priori.normalized();
    _x_a_priori.tail<3>() = drift;
  }

  void calcF(hrp::Matrix77& F,
             const Eigen::Vector4d& q,
             const Eigen::Vector3d& gyro,
             const Eigen::Vector3d& drift) const {
    F = hrp::Matrix77::Identity();
    Eigen::Vector3d gyro_compensated = gyro - drift;
    Eigen::Matrix4d omega;
    calcOmega(omega, gyro_compensated);
    F.block<4, 4>(0, 0) += dt / 2 * omega;
    F.block<4, 3>(0, 4) <<
      + q[1], + q[2], + q[3],
      - q[0], + q[3], - q[2],
      - q[3], - q[0], + q[1],
      + q[2], - q[1], - q[0];
    F.block<4, 3>(0, 4) *= dt / 2;
  }

  void calcPredictedCovariance(hrp::Matrix77& _P_a_priori,
                               const hrp::Matrix77& F,
                               const Eigen::Vector4d& q) const {
    /* P_a_priori = F P F^T + V Q V^T */
    Eigen::Matrix<double, 4, 3> V_upper;
    V_upper <<
      - q[1], - q[2], - q[3],
      + q[0], - q[3], + q[2],
      + q[3], + q[0], - q[1],
      - q[2], + q[1], + q[0];
    V_upper *= dt / 2;
    hrp::Matrix77 VQVt = hrp::Matrix77::Zero();
    VQVt.block<4, 4>(0, 0) = V_upper * Q * V_upper.transpose();
    _P_a_priori = F * P * F.transpose() + VQVt;
  }

  Eigen::Vector3d calcAcc(const Eigen::Vector4d& q) const {
    Eigen::Quaternion<double> q_tmp(q[0], q[1], q[2], q[3]);
    Eigen::Vector3d acc = q_tmp.conjugate()._transformVector(g_vec);
    return acc;
  }

  void calcH(Eigen::Matrix<double, 3, 7>& H, const Eigen::Vector4d& q) const {
    double w = q[0], x = q[1], y = q[2], z = q[3];
    H <<
      -y, +z, -w, +x, 0, 0, 0,
      +x, +w, +z, +y, 0, 0, 0,
      +w, -x, -y, +z, 0, 0, 0;
    H *= 2 * g_vec[2];
  }

  Eigen::Vector3d calcMeasurementResidual(const Eigen::Vector3d& acc_measured,
                                          const Eigen::Vector4d& q) const {
    /* y = z - h(x) */
    Eigen::Vector3d y = acc_measured - calcAcc(q);
    return y;
  }


  void prediction(const Eigen::Vector3d& u) {
    Eigen::Vector4d q = x.head<4>();
    Eigen::Vector3d drift = x.tail<3>();
    hrp::Matrix77 F;
    calcF(F, q, u, drift);
    hrp::Vector7 x_tmp;
    calcPredictedState(x_tmp, q, u, drift);
    x_a_priori = x_tmp;
    hrp::Matrix77 P_tmp;
    calcPredictedCovariance(P_tmp, F, q);
    P_a_priori = P_tmp;
  }

  void correction(const Eigen::Vector3d& z) {
    Eigen::Vector4d q_a_priori = x_a_priori.head<4>();
    Eigen::Matrix<double, 3, 7> H;
    Eigen::Vector3d y = calcMeasurementResidual(z, q_a_priori);
    calcH(H, q_a_priori);
    Eigen::Matrix3d S = H * P_a_priori * H.transpose() + R;
    Eigen::Matrix<double, 7, 3> K = P_a_priori * H.transpose() * S.inverse();
    hrp::Vector7 x_tmp = x_a_priori + K * y;
    x.head<4>() = x_tmp.head<4>().normalized(); /* quaternion */
    x.tail<3>() = x_tmp.tail<3>(); /* bias */
    P = (hrp::Matrix77::Identity() - K * H) * P_a_priori;
  }

  void printAll() const {
    std::cerr << "x" << std::endl << x << std::endl;
    std::cerr << "x_a_priori" << std::endl << x_a_priori << std::endl;
    std::cerr << "P" << std::endl << P << std::endl << std::endl;
    std::cerr << "P_a_priori" << std::endl << P_a_priori << std::endl << std::endl;
  }

  void main_one (hrp::Vector3& rpy, hrp::Vector3& rpyRaw, const hrp::Vector3& acc, const hrp::Vector3& gyro)
  {
    prediction(gyro);
    correction(acc);
    /* ekf_filter.printAll(); */
    Eigen::Quaternion<double> q(x[0], x[1], x[2], x[3]);
    rpy = hrp::rpyFromRot(q.toRotationMatrix());
  };

  void setdt (const double _dt) { dt = _dt;};
private:
  hrp::Vector7 x, x_a_priori;
  hrp::Matrix77 P, P_a_priori;
  Eigen::Matrix3d Q, R;
  Eigen::Vector3d g_vec;
  double dt;
};

#endif /* EKFILTER_H */
