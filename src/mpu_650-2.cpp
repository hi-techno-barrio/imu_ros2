#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <std_msgs/Bool.h>
#include <std_srvs/Empty.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <tf/transform_datatypes.h>


#define MPU_FRAMEID "base_imu"

bool calibrate;
ros::Publisher imu_calib_pub;
ros::ServiceClient * clientptr;

extern "C"{

#include "mpu9150.h"
#include "local_defaults.h"

}

int main(int argc, char **argv){

    ros::init(argc, argv, "mpu_6050");

    ros::NodeHandle pn("~");
    ros::NodeHandle n;


    ROS_INFO("Starting mpu_6050_node...");

    /****
     *IMU parameters
     ***/
    int sample_rate;
    pn.param<int>("frequency", sample_rate ,DEFAULT_SAMPLE_RATE_HZ);
    int i2c_bus;
    pn.param<int>("i2c_bus",i2c_bus,0);
    int yaw_mix_factor;
    pn.param<int>("yaw_mix_factor",yaw_mix_factor,DEFAULT_YAW_MIX_FACTOR);
    std::string frame_id;
    pn.param<std::string>("frame_id",frame_id,MPU_FRAMEID);
    
    /*Covariance*/
    double angular_velocity_covariance,pitch_roll_covariance,yaw_covariance,linear_acceleration_covariance,linear_acceleration_stdev_,angular_velocity_stdev_,yaw_stdev_,pitch_roll_stdev_;
    
    pn.param("linear_acceleration_stdev", linear_acceleration_stdev_, (400 / 1000000.0) * 9.807 ); // NOISE PERFORMANCE: Power Spectral Density @10Hz, AFS_SEL=0 & ODR=1kHz 400 ug/√Hz (probably wrong)
    pn.param("angular_velocity_stdev", angular_velocity_stdev_, 0.05 * (M_PI / 180.0)); // Total RMS Noise: DLPFCFG=2 (100Hz) 0.05 º/s-rms (probably lower (?) @ 42Hz)
    pn.param("pitch_roll_stdev", pitch_roll_stdev_, 1.0 * (M_PI / 180.0));  // 1 degree for pitch and roll
    pn.param("yaw_stdev", yaw_stdev_, 5.0 * (M_PI / 180.0));  // 5 degrees for yaw

    angular_velocity_covariance = angular_velocity_stdev_ * angular_velocity_stdev_;
    linear_acceleration_covariance = linear_acceleration_stdev_ * linear_acceleration_stdev_;
    pitch_roll_covariance = pitch_roll_stdev_ * pitch_roll_stdev_;
    yaw_covariance = yaw_stdev_ * yaw_stdev_;

    ROS_INFO("setting up MPU60X0...");

    mpudata_t mpu;

    //mpu9150_set_debug(1);
    ROS_INFO("Initialize MPU_6050...");
    if (mpu9150_init(i2c_bus,sample_rate, yaw_mix_factor)){
        ROS_FATAL("MPU6050 - %s - MPU6050 connection failed",__FUNCTION__);
        ROS_BREAK();
    }
    memset(&mpu, 0, sizeof(mpudata_t));
    if (sample_rate == 0)
        ROS_BREAK();


    ros::Publisher imu_pub = n.advertise<sensor_msgs::Imu>("imu/data", 10);
    ros::Publisher imu_euler_pub = n.advertise<geometry_msgs::Vector3Stamped>("imu/euler", 10);
    ros::Publisher mag_pub = n.advertise<geometry_msgs::Vector3Stamped>("imu/mag", 10);
    ros::Rate r(sample_rate);

    while(ros::ok())
    {
        ros::Time now = ros::Time::now();

        sensor_msgs::Imu imu_msg;
        geometry_msgs::Vector3Stamped imu_euler_msg;
        imu_msg.header.stamp = now;
        imu_msg.header.frame_id = frame_id;
        imu_euler_msg.header.stamp = now;
        imu_euler_msg.header.frame_id = frame_id;
        geometry_msgs::Vector3Stamped mag_msg;
        mag_msg.header.stamp = now;
        mag_msg.header.frame_id = frame_id;

        if (mpu9150_read(&mpu) == 0) {

            /*imu_msg.orientation.x=mpu.fusedQuat[QUAT_X];
             imu_msg.orientation.y=mpu.fusedQuat[QUAT_Y];
             imu_msg.orientation.z=mpu.fusedQuat[QUAT_Z];
             imu_msg.orientation.w=mpu.fusedQuat[QUAT_W];*/

            tf::Quaternion quat2 =tf::createQuaternionFromRPY(mpu.fusedEuler[VEC3_X],-mpu.fusedEuler[VEC3_Y],-mpu.fusedEuler[VEC3_Z]);
            imu_euler_msg.vector.y=-mpu.fusedEuler[VEC3_Y]*RAD_TO_DEGREE;
            imu_euler_msg.vector.x=mpu.fusedEuler[VEC3_X]*RAD_TO_DEGREE;
            imu_euler_msg.vector.z=-mpu.fusedEuler[VEC3_Z]*RAD_TO_DEGREE;

            imu_msg.orientation.x=quat2.getX();
            imu_msg.orientation.y=quat2.getY();
            imu_msg.orientation.z=quat2.getZ();
            imu_msg.orientation.w=quat2.getW();
	    
	    imu_msg.linear_acceleration_covariance[0] = linear_acceleration_covariance;
	    imu_msg.linear_acceleration_covariance[4] = linear_acceleration_covariance;
	    imu_msg.linear_acceleration_covariance[8] = linear_acceleration_covariance;

	    imu_msg.angular_velocity_covariance[0] = angular_velocity_covariance;
	    imu_msg.angular_velocity_covariance[4] = angular_velocity_covariance;
	    imu_msg.angular_velocity_covariance[8] = angular_velocity_covariance;
    
	    imu_msg.orientation_covariance[0] = pitch_roll_covariance;
	    imu_msg.orientation_covariance[4] = pitch_roll_covariance;
	    imu_msg.orientation_covariance[8] = yaw_covariance;

            //TODO: check if needed
            /*double roll, pitch , yaw;
        tf::Quaternion q(msg->orientation.x,msg->orientation.y,msg->orientation.z,msg->orientation.w);
        tf::Matrix3x3 m(q);
        m.getRPY(roll, pitch, yaw);
        yaw +=yaw_offset;
        tf::Quaternion q_new;
        q_new.setRPY(roll,pitch,yaw);
        imu_corrected.orientation.x=q_new.getX();
        imu_corrected.orientation.y=q_new.getY();
        imu_corrected.orientation.z=q_new.getZ();
        imu_corrected.orientation.w=q_new.getW();*/


            //TODO: verify conversion

            float ax_f, ay_f, az_f;
            float gx_f, gy_f, gz_f;

            ax_f =((float) mpu.calibratedAccel[0]) / (16384 / 9.807); // 2g scale in m/s^2
            ay_f =((float) mpu.calibratedAccel[1]) / (16384 / 9.807); // 2g scale in m/s^2
            az_f =((float) mpu.calibratedAccel[2]) / (16384 / 9.807); // 2g scale in m/s^2

            gx_f=((float) mpu.rawGyro[0]) / 16.4f; // for degrees/s 2000 scale
            gy_f=((float) mpu.rawGyro[1]) / 16.4f; // for degrees/s 2000 scale
            gz_f=((float) mpu.rawGyro[2]) / 16.4f; // for degrees/s 2000 scale

            imu_msg.linear_acceleration.x=-ax_f;
            imu_msg.linear_acceleration.y=ay_f;
            imu_msg.linear_acceleration.z=az_f;

            imu_msg.angular_velocity.x=gx_f;
            imu_msg.angular_velocity.y=gy_f;
            imu_msg.angular_velocity.z=gz_f;

            mag_msg.vector.x=mpu.calibratedMag[VEC3_X];
            mag_msg.vector.y=mpu.calibratedMag[VEC3_Y];
            mag_msg.vector.z=mpu.calibratedMag[VEC3_Z];

            imu_pub.publish(imu_msg);
            imu_euler_pub.publish(imu_euler_msg);
            mag_pub.publish(mag_msg);


        }else{
            ROS_WARN("MPU6050 - %s - MPU6050 read failed",__FUNCTION__);
        }

        ros::spinOnce();
        r.sleep();
    }



    mpu9150_exit();

    return 0;
}
