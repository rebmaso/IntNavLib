from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    main_node = Node(
        package='ros2_lc_ins_gnss_ecef',
        executable='ros2_lc_ins_gnss_ecef',
        name='ros2_lc_ins_gnss_ecef',
        output='screen',
        parameters=['config/Profile_3_config.yaml'],  # Load parameters from YAML
    )
    return LaunchDescription([main_node])