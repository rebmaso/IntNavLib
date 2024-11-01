#include "helpers.h"

namespace intnavlib {

NavSolutionEcef nedToEcef(const NavSolutionNed& nav_sol_ned) {

    // Extract the inputs
    double L_b = nav_sol_ned.latitude;
    double lambda_b = nav_sol_ned.longitude;
    double h_b = nav_sol_ned.height;
    Eigen::Vector3d v_eb_n = nav_sol_ned.v_eb_n;
    Eigen::Matrix3d C_b_n = nav_sol_ned.C_b_n;
    
    double sin_lat = std::sin(L_b);
    double cos_lat = std::cos(L_b);
    double sin_long = std::sin(lambda_b);
    double cos_long = std::cos(lambda_b);

    // Compute transverse radius of curvature
    double R_E = R_0 / std::sqrt(1.0 - pow((e * sin_lat),2.0));

    // Convert position from curvilinear to Cartesian ECEF coordinates
    Eigen::Vector3d r_eb_e;
    r_eb_e(0) = (R_E + h_b) * cos_lat * cos_long;
    r_eb_e(1) = (R_E + h_b) * cos_lat * sin_long;
    r_eb_e(2) = ((1.0 - e * e) * R_E + h_b) * sin_lat;

    // Compute the ECEF to NED coordinate transformation matrix
    Eigen::Matrix3d C_e_n;
    C_e_n << -sin_lat * cos_long, -sin_lat * sin_long, cos_lat,
             -sin_long,            cos_long,          0.0,
             -cos_lat * cos_long, -cos_lat * sin_long, -sin_lat;

    // Transform velocity from NED to ECEF frame
    Eigen::Vector3d v_eb_e = C_e_n.transpose() * v_eb_n;

    // Transform attitude from NED to ECEF frame
    Eigen::Matrix3d C_b_e = C_e_n.transpose() * C_b_n;

    // Construct the output structure
    NavSolutionEcef nav_sol_ecef;
    nav_sol_ecef.time = nav_sol_ned.time;
    nav_sol_ecef.r_eb_e = r_eb_e;
    nav_sol_ecef.v_eb_e = v_eb_e;
    nav_sol_ecef.C_b_e = C_b_e;

    return nav_sol_ecef;
}

NavSolutionNed ecefToNed(const NavSolutionEcef & nav_sol_ecef){

    // Convert position using Borkowski closed-form exact solution
    // From (2.113)
    double lambda_b = atan2(nav_sol_ecef.r_eb_e(1), nav_sol_ecef.r_eb_e(0));

    // From (C.29) and (C.30)
    double k1 = sqrt(1.0 - pow(e,2.0)) * abs(nav_sol_ecef.r_eb_e(2));
    double k2 = pow(e,2.0) * R_0;
    double beta = sqrt(pow(nav_sol_ecef.r_eb_e(0),2.0) + pow(nav_sol_ecef.r_eb_e(1),2.0));
    double E = (k1 - k2) / beta;
    double F = (k1 + k2) / beta;

    // From (C.31)
    double P = 4.0/3.0 * (E*F + 1.0);

    // From (C.32)
    double Q = 2.0 * (pow(E,2.0) - pow(F,2.0));

    // From (C.33)
    double D = pow(P,3.0) + pow(Q,2.0);

    // From (C.34)
    double V = pow(sqrt(D) - Q, 1.0/3.0) - pow(sqrt(D) + Q,1.0/3.0);

    // From (C.35)
    double G = 0.5 * (sqrt(pow(E,2.0) + V) + E);

    // From (C.36)
    double T = sqrt(pow(G,2.0) + (F - V * G) / (2.0 * G - E)) - G;

    // From (C.37)
    double L_b = sgn(nav_sol_ecef.r_eb_e(2)) * atan((1.0 - pow(T,2.0)) / (2.0 * T * sqrt (1.0 - pow(e,2.0))));

    // From (C.38)
    double h_b = (beta - R_0 * T) * cos(L_b) +
        (nav_sol_ecef.r_eb_e(2) - sgn(nav_sol_ecef.r_eb_e(2)) * R_0 * sqrt(1.0 - pow(e,2.0))) * sin(L_b);
        
    // Calculate ECEF to NED coordinate transformation matrix using (2.150)
    double cos_lat = cos(L_b);
    double sin_lat = sin(L_b);
    double cos_long = cos(lambda_b);
    double sin_long = sin(lambda_b);

    Eigen::Matrix3d C_e_n;
    C_e_n << -sin_lat * cos_long, -sin_lat * sin_long,  cos_lat,
                    -sin_long,            cos_long,        0.0,
            -cos_lat * cos_long, -cos_lat * sin_long, -sin_lat;
        
    // Transform velocity using (2.73)
    Eigen::Vector3d v_eb_n = C_e_n * nav_sol_ecef.v_eb_e;

    // Transform attitude using (2.15)
    Eigen::Matrix3d C_b_n = C_e_n * nav_sol_ecef.C_b_e;

    NavSolutionNed nav_sol_ned;
    nav_sol_ned.time = nav_sol_ecef.time;
    nav_sol_ned.latitude = L_b;
    nav_sol_ned.longitude = lambda_b;
    nav_sol_ned.height = h_b;
    nav_sol_ned.v_eb_n = v_eb_n;
    nav_sol_ned.C_b_n = C_b_n;

    return nav_sol_ned;

}


Eigen::Matrix3d rpyToR(const Eigen::Vector3d & rpy) {

    double roll = rpy(0);
    double pitch = rpy(1);
    double yaw = rpy(2);

    // Precompute sines and cosines of Euler angles
    double sin_phi = std::sin(roll);
    double cos_phi = std::cos(roll);
    double sin_theta = std::sin(pitch);
    double cos_theta = std::cos(pitch);
    double sin_psi = std::sin(yaw);
    double cos_psi = std::cos(yaw);

    // Calculate the coordinate transformation matrix using the provided equations
    Eigen::Matrix3d C;
    C(0,0) = cos_theta * cos_psi;
    C(0,1) = cos_theta * sin_psi;
    C(0,2) = -sin_theta;
    C(1,0) = -cos_phi * sin_psi + sin_phi * sin_theta * cos_psi;
    C(1,1) = cos_phi * cos_psi + sin_phi * sin_theta * sin_psi;
    C(1,2) = sin_phi * cos_theta;
    C(2,0) = sin_phi * sin_psi + cos_phi * sin_theta * cos_psi;
    C(2,1) = -sin_phi * cos_psi + cos_phi * sin_theta * sin_psi;
    C(2,2) = cos_phi * cos_theta;

    return C;
}

Eigen::Vector3d rToRpy(const Eigen::Matrix3d & C) {

    Eigen::Vector3d rpy;

    rpy(0) = atan2(C(1,2),C(2,2));
    rpy(1) = - asin(C(0,2));      
    rpy(2) = atan2(C(0,1),C(0,0));

    return rpy;
}

Eigen::Vector3d gravityEcef(const Eigen::Vector3d & r_eb_e) {

    double mag_r = r_eb_e.norm();

    Eigen::Vector3d g = Eigen::Vector3d::Zero();
    // If the input position is 0,0,0, produce a dummy output

    if (mag_r >= EPSILON)
    // Calculate gravitational acceleration using (2.142)
    {
        double z_scale = 5.0 * pow(r_eb_e(2) / mag_r,2.0);

        Eigen::Vector3d gamma_1;
        gamma_1 << (1.0 - z_scale) * r_eb_e(0), 
                    (1.0 - z_scale) * r_eb_e(1),
                    (3.0 - z_scale) * r_eb_e(2);

        Eigen::Vector3d gamma;
        gamma = (-mu / pow(mag_r,3.0)) *
                (r_eb_e + 1.5 * J_2 * 
                pow(R_0 / mag_r,2.0) * gamma_1);

        // Add centripetal acceleration using (2.133)
        g(0) = gamma(0) + pow(omega_ie,2.0) * r_eb_e(0);
        g(1) = gamma(1) + pow(omega_ie,2.0) * r_eb_e(1);
        g(2) = gamma(2);
    }

    return g;

}

Eigen::Matrix3d skewSymmetric(const Eigen::Vector3d & a) {
    
    Eigen::Matrix3d S;
    
    S << 0.0, -a(2),  a(1),
      a(2),     0.0, -a(0),
     -a(1),  a(0),     0.0;

    return S;
}

Eigen::Vector3d deSkew(const Eigen::Matrix3d & S){

    Eigen::Vector3d a;

    a << -S(1,2), S(0,2), -S(0,1);

    return a; 
}

// Function to get the current date and time as a formatted string
std::string getCurrentDateTime() {
    auto now = std::time(nullptr);
    std::tm tm_now;
    localtime_r(&now, &tm_now); // Use localtime_s on Windows or localtime_r on Unix-like systems

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y%m%d_%H%M%S");
    return oss.str();
}

Eigen::Vector2d radiiOfCurvature(double L) {

    // Calculate meridian radius of curvature using (2.105)
    double temp = 1.0 - pow((e * sin(L)),2.0); 
    double R_N = R_0 * (1.0 - pow(e,2.0)) / pow(temp,1.5);
    // Calculate transverse radius of curvature using (2.105)
    double R_E = R_0 / sqrt(temp);
    Eigen::Vector2d radii;
    radii << R_N, R_E;
    return radii;
}

ErrorsNed calculateErrorsNed(const NavSolutionNed & true_nav_sol, 
                                const NavSolutionNed & est_nav_sol){

    // Position error calculation
    Eigen::Vector2d radii = radiiOfCurvature(true_nav_sol.latitude);
    double R_N = radii(0);
    double R_E = radii(1);

    Eigen::Vector3d delta_r_eb_n;
    delta_r_eb_n(0) = (est_nav_sol.latitude - true_nav_sol.latitude) * (R_N + true_nav_sol.height);
    delta_r_eb_n(1) = (est_nav_sol.longitude - true_nav_sol.longitude) * (R_E + true_nav_sol.height) * std::cos(true_nav_sol.latitude);
    delta_r_eb_n(2) = -(est_nav_sol.height - true_nav_sol.height);

    // Velocity error calculation
    Eigen::Vector3d delta_v_eb_n = est_nav_sol.v_eb_n - true_nav_sol.v_eb_n;

    // Attitude error calculation
    Eigen::Matrix3d delta_C_b_n = est_nav_sol.C_b_n * true_nav_sol.C_b_n.transpose();
    Eigen::Vector3d delta_eul_nb_n = -rToRpy(delta_C_b_n);

    ErrorsNed errors_ned;
    errors_ned.time = true_nav_sol.time;
    errors_ned.delta_r_eb_n = delta_r_eb_n;
    errors_ned.delta_v_eb_n = delta_v_eb_n;
    errors_ned.delta_eul_nb_n = delta_eul_nb_n;

    return errors_ned;
}

GnssLsPosVelClock gnssLsPositionVelocityClock(const GnssMeasurements & gnss_measurements,
                                    const Eigen::Vector3d & prior_r_ea_e,
                                    const Eigen::Vector3d & prior_v_ea_e) {
    
    GnssLsPosVelClock est_pos_vel;

    // POSITION AND CLOCK OFFSET
    Eigen::Vector4d x_pred, x_est;
    x_pred.segment<3>(0) = prior_r_ea_e;
    x_pred(3) = 0;
    double test_convergence = 1.0;

    while (test_convergence > 0.0001) {
        Eigen::Matrix<double, Eigen::Dynamic, 4, 0, MAX_GNSS_SATELLITES> H_matrix(gnss_measurements.no_meas, 4);
        Eigen::Matrix<double, Eigen::Dynamic, 1, 0, MAX_GNSS_SATELLITES> pred_meas(gnss_measurements.no_meas);
        
        for (int j = 0; j < gnss_measurements.no_meas; ++j) {
            Eigen::Vector3d delta_r = gnss_measurements.meas.block<1,3>(j,2).transpose() - x_pred.segment<3>(0);
            double approx_range = delta_r.norm();

            Eigen::Matrix3d C_e_I;
            C_e_I << 1, omega_ie * approx_range / c, 0,
                     -omega_ie * approx_range / c, 1, 0,
                     0, 0, 1;

            delta_r = C_e_I * gnss_measurements.meas.block<1,3>(j,2).transpose() - x_pred.segment<3>(0);
            double range = delta_r.norm();
            pred_meas(j) = range + x_pred(3);
            
            H_matrix.block<1,3>(j,0) = -delta_r.transpose() / range;
            H_matrix(j,3) = 1;
        }

        x_est = x_pred + (H_matrix.transpose() * H_matrix).inverse() * H_matrix.transpose() * (gnss_measurements.meas.col(0).head(gnss_measurements.no_meas) - pred_meas.head(gnss_measurements.no_meas));

        test_convergence = (x_est - x_pred).norm();
        x_pred = x_est;
    }

    est_pos_vel.r_ea_e = x_est.segment<3>(0);
    est_pos_vel.clock(0) = x_est(3);

    // VELOCITY AND CLOCK DRIFT
    Eigen::Matrix3d Omega_ie = skewSymmetric(Eigen::Vector3d(0, 0, omega_ie));
    
    x_pred.segment<3>(0) = prior_v_ea_e;
    x_pred(3) = 0;
    test_convergence = 1.0;

    while (test_convergence > 0.0001) {
        Eigen::Matrix<double, Eigen::Dynamic, 4, 0, MAX_GNSS_SATELLITES> H_matrix(gnss_measurements.no_meas, 4);
        Eigen::Matrix<double, Eigen::Dynamic, 1, 0, MAX_GNSS_SATELLITES> pred_meas(gnss_measurements.no_meas);
        
        for (int j = 0; j < gnss_measurements.no_meas; ++j) {
            Eigen::Vector3d delta_r = gnss_measurements.meas.block<1,3>(j,2).transpose() - est_pos_vel.r_ea_e;
            double approx_range = delta_r.norm();

            Eigen::Matrix3d C_e_I;
            C_e_I << 1, omega_ie * approx_range / c, 0,
                     -omega_ie * approx_range / c, 1, 0,
                     0, 0, 1;

            delta_r = C_e_I * gnss_measurements.meas.block<1,3>(j,2).transpose() - est_pos_vel.r_ea_e;
            double range = delta_r.norm();
            Eigen::Vector3d u_as_e = delta_r / range;

            Eigen::Vector3d sat_velocity = gnss_measurements.meas.block<1,3>(j,5).transpose();
            Eigen::Vector3d sat_position = gnss_measurements.meas.block<1,3>(j,2).transpose();

            double range_rate = u_as_e.transpose() * (C_e_I * (sat_velocity + Omega_ie * sat_position) - (x_pred.segment<3>(0) + Omega_ie * est_pos_vel.r_ea_e));
            pred_meas(j) = range_rate + x_pred(3);
            
            H_matrix.block<1,3>(j,0) = -u_as_e.transpose();
            H_matrix(j,3) = 1;
        }

        x_est = x_pred + (H_matrix.transpose() * H_matrix).inverse() * H_matrix.transpose() * (gnss_measurements.meas.col(1).head(gnss_measurements.no_meas) - pred_meas.head(gnss_measurements.no_meas));

        test_convergence = (x_est - x_pred).norm();
        x_pred = x_est;
    }

    est_pos_vel.v_ea_e = x_est.segment<3>(0);
    est_pos_vel.clock(1) = x_est(3);

    return est_pos_vel;
}


};