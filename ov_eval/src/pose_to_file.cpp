/*
 * OpenVINS: An Open Platform for Visual-Inertial Research
 * Copyright (C) 2019 Patrick Geneva
 * Copyright (C) 2019 Kevin Eckenhoff
 * Copyright (C) 2019 Guoquan Huang
 * Copyright (C) 2019 OpenVINS Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TransformStamped.h>


#include "utils/Recorder.h"



int main(int argc, char **argv) {

    // Create ros node
    ros::init(argc, argv, "pose_to_file");
    ros::NodeHandle nh("~");

    // Get parameters to subscribe
    std::string topic, topic_type, fileoutput;
    nh.getParam("topic", topic);
    nh.getParam("topic_type", topic_type);
    nh.getParam("output", fileoutput);

    // Debug
    ROS_INFO("Done reading config values");
    ROS_INFO(" - topic = %s", topic.c_str());
    ROS_INFO(" - topic_type = %s", topic_type.c_str());
    ROS_INFO(" - file = %s", fileoutput.c_str());

    // std::string topic_rect, topic_type_rect, fileoutput_rect;
    // nh.getParam("topic_rect", topic_rect);
    // nh.getParam("topic_type_rect", topic_type_rect);
    // nh.getParam("output_rect", fileoutput_rect);

    // ROS_INFO("Done reading config values");
    // ROS_INFO(" - topic_rect = %s", topic_rect.c_str());
    // ROS_INFO(" - topic_type_rect = %s", topic_type_rect.c_str());
    // ROS_INFO(" - file_rect = %s", fileoutput_rect.c_str());


    // Create the recorder object
    ov_eval::Recorder recorder(fileoutput);

    // ov_eval::Recorder recorder_rect(fileoutput_rect);

    // Subscribe to topic
    ros::Subscriber sub;
    if (topic_type == std::string("PoseWithCovarianceStamped")) {
        sub = nh.subscribe(topic, 9999, &ov_eval::Recorder::callback_posecovariance, &recorder);
    } else if (topic_type == std::string("PoseStamped")) {
        sub = nh.subscribe(topic, 9999, &ov_eval::Recorder::callback_pose, &recorder);
    } else if (topic_type == std::string("TransformStamped")) {
        sub = nh.subscribe(topic, 9999, &ov_eval::Recorder::callback_transform, &recorder);
    } else if(topic_type == std::string("Odometry")) {
        sub = nh.subscribe(topic, 9999, &ov_eval::Recorder::callback_odometry, &recorder);
    } else {
        ROS_ERROR("The specified topic type is not supported");
        ROS_ERROR("topic_type = %s", topic_type.c_str());
        ROS_ERROR("please select from: PoseWithCovarianceStamped, PoseStamped, TransformStamped, Odometry");
        std::exit(EXIT_FAILURE);
    }
    
    // ros::Subscriber sub_rect;
    // if (topic_type_rect == std::string("PoseWithCovarianceStamped")) {
    //     sub_rect = nh.subscribe(topic_rect, 9999, &ov_eval::Recorder::callback_posecovariance, &recorder_rect);
    // } else if (topic_type_rect == std::string("PoseStamped")) {
    //     sub_rect = nh.subscribe(topic_rect, 9999, &ov_eval::Recorder::callback_pose, &recorder_rect);
    // } else if (topic_type_rect == std::string("TransformStamped")) {
    //     sub_rect = nh.subscribe(topic_rect, 9999, &ov_eval::Recorder::callback_transform, &recorder_rect);
    // } else if(topic_type == std::string("Odometry")) {
    //     sub_rect = nh.subscribe(topic_rect, 9999, &ov_eval::Recorder::callback_odometry, &recorder_rect);
    // } else {
    //     ROS_ERROR("The specified topic type is not supported");
    //     ROS_ERROR("topic_type = %s", topic_type_rect.c_str());
    //     ROS_ERROR("please select from: PoseWithCovarianceStamped, PoseStamped, TransformStamped, Odometry");
    //     std::exit(EXIT_FAILURE);
    // }

    // Done!
    ros::spin();
    return EXIT_SUCCESS;

}



