#include <ros/ros.h>

#include <sensor_msgs/PointCloud.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>

// Rmagine deps
#include <rmagine/map/EmbreeMap.hpp>
#include <rmagine/util/StopWatch.hpp>
#include <rmagine/math/math.cuh>

// RCML msgs
#include <rmcl_msgs/ScanStamped.h>

// RMCL code
#include <rmcl/correction/LiDARCorrectorOptix.hpp>
#include <rmcl/util/conversions.h>
#include <rmcl/util/scan_operations.h>

// rosmath
#include <rosmath/sensor_msgs/conversions.h>
#include <rosmath/sensor_msgs/math.h>
#include <rosmath/eigen/conversions.h>

#include <chrono>
#include <memory>
#include <omp.h>

#include <Eigen/Dense>

using namespace rosmath;
using namespace rmcl;
using namespace rmcl_msgs;
using namespace rmagine;

// LiDARCorrectorEmbreePtr scan_correct;
LiDARCorrectorOptixPtr scan_correct;
ros::Publisher cloud_pub;
ros::Publisher pose_pub;

bool        pose_received = false;
ros::Time   last_pose;
bool        scan_received = false;
ros::Time   last_scan;

std::string map_frame;
std::string odom_frame;
bool has_odom_frame = true;
std::string base_frame;
bool has_base_frame = true;
std::string sensor_frame;

std::shared_ptr<tf2_ros::Buffer> tfBuffer;
std::shared_ptr<tf2_ros::TransformListener> tfListener; 

// Estimate this
geometry_msgs::TransformStamped T_odom_map;
// dynamic: ekf
geometry_msgs::TransformStamped T_base_odom;
// static: urdf
geometry_msgs::TransformStamped T_sensor_base;

StopWatch sw;

/**
 * @brief Update T_sensor_base and T_base_odom globally
 */
bool fetchTF()
{
    bool ret = true;

    if(has_base_frame)
    {
        try{
            T_sensor_base = tfBuffer->lookupTransform(base_frame, sensor_frame, ros::Time(0));
        }
        catch (tf2::TransformException &ex) {
            ROS_WARN("%s", ex.what());
            ROS_WARN_STREAM("Source: " << base_frame << ", Target: " << sensor_frame);

            ret = false;
        }
    } else {
        T_sensor_base.header.frame_id = base_frame;
        T_sensor_base.child_frame_id = sensor_frame;
        T_sensor_base.transform.translation.x = 0.0;
        T_sensor_base.transform.translation.y = 0.0;
        T_sensor_base.transform.translation.z = 0.0;
        T_sensor_base.transform.rotation.x = 0.0;
        T_sensor_base.transform.rotation.y = 0.0;
        T_sensor_base.transform.rotation.z = 0.0;
        T_sensor_base.transform.rotation.w = 1.0;
    }

    scan_correct->setTsb(T_sensor_base.transform);
    
    if(has_odom_frame && has_base_frame)
    {
        try{
            T_base_odom = tfBuffer->lookupTransform(odom_frame, base_frame, ros::Time(0));
        }
        catch (tf2::TransformException &ex) {
            ROS_WARN("%s", ex.what());
            ROS_WARN_STREAM("Source: " << odom_frame << ", Target: " << base_frame);
            ret = false;
        }
    } else {
        T_base_odom.header.frame_id = odom_frame;
        T_base_odom.child_frame_id = base_frame;
        T_base_odom.transform.translation.x = 0.0;
        T_base_odom.transform.translation.y = 0.0;
        T_base_odom.transform.translation.z = 0.0;
        T_base_odom.transform.rotation.x = 0.0;
        T_base_odom.transform.rotation.y = 0.0;
        T_base_odom.transform.rotation.z = 0.0;
        T_base_odom.transform.rotation.w = 1.0;
    }

    return ret;
}

// Storing Pose information globally
// Calculate transformation from map to odom from pose in map frame
void poseCB(geometry_msgs::PoseStamped msg)
{
    // std::cout << "poseCB" << std::endl;
    map_frame = msg.header.frame_id;
    pose_received = true;

    // set T_base_map
    geometry_msgs::TransformStamped T_base_map;
    T_base_map.header.frame_id = map_frame;
    T_base_map.child_frame_id = base_frame;
    T_base_map.transform <<= msg.pose;

    fetchTF();

    T_odom_map = T_base_map * ~T_base_odom;
}

// Storing scan information globally
// updating real data inside the global scan corrector
void scanCB(const ScanStamped::ConstPtr& msg)
{
    sensor_frame = msg->header.frame_id;
    scan_correct->setModelAndInputData(msg->scan);
    last_scan = msg->header.stamp;
    scan_received = true;
}

void correctOnce()
{
    // std::cout << "correctOnce" << std::endl;
    // 1. Get Base in Map
    geometry_msgs::TransformStamped T_base_map = T_odom_map * T_base_odom;
    
    Memory<Transform, RAM> poses(1);
    convert(T_base_map.transform, poses[0]);
    // upload to GPU
    Memory<Transform, VRAM_CUDA> poses_(1);
    poses_ = poses;
    auto corrRes = scan_correct->correct(poses_);
    poses_ = multNxN(poses_, corrRes.Tdelta);
    // download to CPU
    poses = poses_;


    // Update T_odom_map
    convert(poses[0], T_base_map.transform);
    T_odom_map = T_base_map * ~T_base_odom;
}

void updateTF()
{
    // std::cout << "updateTF" << std::endl;
    static tf2_ros::TransformBroadcaster br;
    
    geometry_msgs::TransformStamped T;

    // What is the source frame?
    if(has_odom_frame && has_base_frame)
    {
        // With EKF and base_frame: Send odom to map
        T = T_odom_map;
    } else if(has_base_frame) {
        // With base but no EKF: send base to map
        T = T_odom_map * T_base_odom;
    } else {
        // Default:
        // Sensor to map
        T = T_odom_map * T_base_odom * T_sensor_base;
    }

    T.header.stamp = last_scan;
    T.header.frame_id = map_frame;

    br.sendTransform(T);
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "lidar_corrector_embree");
    ros::NodeHandle nh;
    ros::NodeHandle nh_p("~");

    ROS_INFO("Embree Corrector started");

    std::string map_frame;
    std::string meshfile;

    nh_p.param<std::string>("map_file", meshfile, "/home/amock/ros_workspaces/amcl_flex/avz_floor.ply");

    // Default minimal setting: Map and Sensor
    nh_p.param<std::string>("odom_frame", odom_frame, "");
    nh_p.param<std::string>("base_frame", base_frame, "");

    if(base_frame == "")
    {
        has_base_frame = false;
    }

    if(odom_frame == "")
    {
        has_odom_frame = false;
    }

    EmbreeMapPtr map = importEmbreeMap(meshfile);
    
    scan_correct.reset(new LiDARCorrectorEmbreeROS(map));

    CorrectionParams corr_params;
    nh_p.param<float>("max_distance", corr_params.max_distance, 0.5);
    scan_correct->setParams(corr_params);

    std::cout << "Max Distance: " << corr_params.max_distance << std::endl;

    // get TF of scanner
    tfBuffer.reset(new tf2_ros::Buffer);
    tfListener.reset(new tf2_ros::TransformListener(*tfBuffer));

    cloud_pub = nh_p.advertise<sensor_msgs::PointCloud>("sim_cloud", 1);
    pose_pub = nh_p.advertise<geometry_msgs::PoseStamped>("sim_pose", 1);
    ros::Subscriber sub = nh.subscribe<ScanStamped>("scan", 1, scanCB);
    ros::Subscriber pose_sub = nh.subscribe<geometry_msgs::PoseStamped>("pose", 1, poseCB);

    ROS_INFO_STREAM(ros::this_node::getName() << ": Open RViz. Set fixed frame to map frame. Set goal. ICP to Mesh");

    ros::Duration d(0.1);

    while(ros::ok())
    {
        if(pose_received && scan_received)
        {
            fetchTF();
            sw();
            correctOnce();
            double el = sw();

            ROS_INFO_STREAM("correctOnce: " << el << "s");
            updateTF();

            double sleep_left = d.toSec() - el;

            if(sleep_left > 0.0)
            {
                ros::Duration d_left(sleep_left);
                d_left.sleep();
            }
        } else {
            d.sleep();
        }
        
        ros::spinOnce();
    }
    
    return 0;
}