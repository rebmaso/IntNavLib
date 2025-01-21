#include <chrono>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "intnavlib/intnavlib.h"

using namespace std::chrono_literals;
using namespace intnavlib;

class InsGnssNode : public rclcpp::Node {
public:
    InsGnssNode() : Node("ins_gnss_node"), current_time_(0.0) {

        // Initialize variables from ROS2 parameters
        init();

        // Initialize subscribers and publisher
        imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
            imu_topic_, 10, std::bind(&InsGnssNode::imuCallback, this, std::placeholders::_1));
        gnss_sub_ = this->create_subscription<sensor_msgs::msg::NavSatFix>(
            gnss_topic_, 10, std::bind(&InsGnssNode::gnssCallback, this, std::placeholders::_1));
        pose_pub_ = this->create_publisher<geometry_msgs::msg::PoseStamped>(output_topic_, 10);

        // Start processing thread
        processing_thread_ = std::thread(&InsGnssNode::processData, this);
    }

    ~InsGnssNode() {
        processing_thread_.join();
    }

private:
    std::string imu_topic_;
    std::string gnss_topic_;
    std::string output_topic_;

    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Subscription<sensor_msgs::msg::NavSatFix>::SharedPtr gnss_sub_;
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_pub_;

    // Buffers
    std::queue<sensor_msgs::msg::Imu::SharedPtr> imu_buffer_;
    std::queue<sensor_msgs::msg::NavSatFix::SharedPtr> gnss_buffer_;
    std::mutex buffer_mutex_;

    // Init params
    std::vector<double> init_r_eb_e;
    std::vector<double> init_v_eb_e;
    std::vector<double> init_rpy_b_e_;

    // Navigation state
    NavSolutionEcef est_nav_ecef_;
    Eigen::Vector3d est_acc_bias_;
    Eigen::Vector3d est_gyro_bias_;
    Eigen::Matrix<double, 15, 15> P_matrix_;

    // EKF config
    KfConfig lc_kf_config_;

    double current_time_;
    std::thread processing_thread_;

    void init() {

        // Topics 
        imu_topic_ = get_parameter("imu_topic").as_string();
        gnss_topic_ = get_parameter("gnss_topic").as_string();

        // Load KF Config
        lc_kf_config_.init_att_unc = get_parameter("kf_config.init_att_unc").as_double();
        lc_kf_config_.init_vel_unc = get_parameter("kf_config.init_vel_unc").as_double();
        lc_kf_config_.init_pos_unc = get_parameter("kf_config.init_pos_unc").as_double();
        lc_kf_config_.init_b_a_unc = get_parameter("kf_config.init_b_a_unc").as_double();
        lc_kf_config_.init_b_g_unc = get_parameter("kf_config.init_b_g_unc").as_double();
        lc_kf_config_.gyro_noise_PSD = get_parameter("kf_config.gyro_noise_PSD").as_double();
        lc_kf_config_.accel_noise_PSD = get_parameter("kf_config.accel_noise_PSD").as_double();
        lc_kf_config_.accel_bias_PSD = get_parameter("kf_config.accel_bias_PSD").as_double();
        lc_kf_config_.gyro_bias_PSD = get_parameter("kf_config.gyro_bias_PSD").as_double();

        // Initialize navigation state
        init_r_eb_e = get_parameter("init_r_eb_e").as_double_array();
        init_v_eb_e = get_parameter("init_v_eb_e").as_double_array();
        init_rpy_b_e_ = get_parameter("init_rpy_b_e_").as_double_array();
        est_nav_ecef_.r_eb_e << init_r_eb_e[0], init_r_eb_e[1], init_r_eb_e[2];
        est_nav_ecef_.v_eb_e << init_v_eb_e[0], init_v_eb_e[1], init_v_eb_e[2];
        est_nav_ecef_.C_b_e << rpyToR(Eigen::Vector3d(init_rpy_b_e_[0], init_rpy_b_e_[1], init_rpy_b_e_[2]));
        est_acc_bias_ = Eigen::Vector3d::Zero();
        est_gyro_bias_ = Eigen::Vector3d::Zero();
        P_matrix_ = InitializeLcPMmatrix(lc_kf_config_);

        // Log loaded parameters for debugging
        RCLCPP_INFO(this->get_logger(), "Loaded initial position: [%f, %f, %f]",
                    init_r_eb_e[0], init_r_eb_e[1], init_r_eb_e[2]);
        RCLCPP_INFO(this->get_logger(), "Loaded initial attitude: [%f, %f, %f, %f]",
                    init_rpy_b_e_[0], init_rpy_b_e_[1], init_rpy_b_e_[2], init_rpy_b_e_[3]);
        RCLCPP_INFO(this->get_logger(), "IMU topic: %s", imu_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "GNSS topic: %s", gnss_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "KF Config: init_att_unc = %f, init_vel_unc = %f",
                    lc_kf_config_.init_att_unc, lc_kf_config_.init_vel_unc);
    }

    void imuCallback(const sensor_msgs::msg::Imu::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        imu_buffer_.push(msg);
    }

    void gnssCallback(const sensor_msgs::msg::NavSatFix::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        gnss_buffer_.push(msg);
    }

    void processData() {
        while (rclcpp::ok()) {
            sensor_msgs::msg::Imu::SharedPtr imu_msg;
            sensor_msgs::msg::NavSatFix::SharedPtr gnss_msg;

            // Threadsafe read from buffers
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);

                if (!imu_buffer_.empty()) {
                    if(imu_buffer_.front()->header.stamp.sec > current_time_)
                        imu_msg = imu_buffer_.front();
                    imu_buffer_.pop();
                }

                if (!gnss_buffer_.empty() {
                    if(gnss_buffer_.front()->header.stamp.sec > current_time_)
                        gnss_msg = gnss_buffer_.front();
                    gnss_buffer_.pop();
                }
            }

            if (imu_msg) {

                ImuMeasurements imu_meas;
                imu_meas.f = Eigen::Vector3d(
                    imu_msg->linear_acceleration.x,
                    imu_msg->linear_acceleration.y,
                    imu_msg->linear_acceleration.z);
                imu_meas.omega = Eigen::Vector3d(
                    imu_msg->angular_velocity.x,
                    imu_msg->angular_velocity.y,
                    imu_msg->angular_velocity.z);
                imu_meas.time = imu_msg->

                double tor_i = 

                // Apply bias corrections
                imu_meas.f -= est_acc_bias_;
                imu_meas.omega -= est_gyro_bias_;

                // Predict navigation state
                double dt = 0.01; // Example time step
                est_nav_ecef_ = navEquationsEcef(est_nav_ecef_, imu_meas, tor_i);

                // Propagate uncertainties
                NavSolutionNed est_nav_ned = ecefToNed(est_nav_ecef_);
                P_matrix_ = lcPropUnc(P_matrix_, est_nav_ecef_, est_nav_ned, imu_meas, lc_kf_config_, tor_i);
            }

            if (gnss_msg) {
                GnssPosVelMeasEcef gnss_meas;
                gnss_meas.r_ea_e = nedToEcef({gnss_msg->latitude, gnss_msg->longitude, gnss_msg->altitude}).r_eb_e;
                gnss_meas.v_ea_e = Eigen::Vector3d::Zero(); // Populate with realistic velocity if available
                gnss_meas.cov_mat = Eigen::Matrix<double, 6, 6>::Identity(); // Covariance matrix
                gnss_meas.

                // Update navigation state using GNSS
                StateEstEcefLc est_state_ecef_prior{est_nav_ecef_, P_matrix_, est_acc_bias_, est_gyro_bias_};
                auto est_state_ecef_post = lcUpdateKFGnssEcef(gnss_meas, est_state_ecef_prior);

                // Update state variables
                est_nav_ecef_ = est_state_ecef_post.nav_sol;
                est_acc_bias_ = est_state_ecef_post.acc_bias;
                est_gyro_bias_ = est_state_ecef_post.gyro_bias;
                P_matrix_ = est_state_ecef_post.P_matrix;
            }

            if (imu_msg || gnss_msg) {
                current_time_ = std::max(current_time_, (imu_msg ? imu_msg->header.stamp.sec : 0.0));
                current_time_ = std::max(current_time_, (gnss_msg ? gnss_msg->header.stamp.sec : 0.0));
                publishPose();
            }

            std::this_thread::sleep_for(1ms); // Sleep to prevent busy-waiting
        }
    }

    void publishPose() {
        auto pose_msg = geometry_msgs::msg::PoseStamped();
        pose_msg.header.stamp = this->now();
        pose_msg.header.frame_id = "world";

        pose_msg.pose.position.x = est_nav_ecef_.r_eb_e[0];
        pose_msg.pose.position.y = est_nav_ecef_.r_eb_e[1];
        pose_msg.pose.position.z = est_nav_ecef_.r_eb_e[2];

        Eigen::Quaterniond q(est_nav_ecef_.C_b_e);
        pose_msg.pose.orientation.x = q.x();
        pose_msg.pose.orientation.y = q.y();
        pose_msg.pose.orientation.z = q.z();
        pose_msg.pose.orientation.w = q.w();

        pose_pub_->publish(pose_msg);
    }
};

int main(int argc, char *argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<InsGnssNode>());
    rclcpp::shutdown();
    return 0;
}
