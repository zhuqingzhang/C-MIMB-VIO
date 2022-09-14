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


#include <string>
#include <iostream>
#include <fstream>
#include <Eigen/Eigen>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "alignment/AlignTrajectory.h"
#include "alignment/AlignUtils.h"
#include "utils/Loader.h"
#include "utils/Math.h"
#include "utils/Colors.h"


#ifdef HAVE_PYTHONLIBS

// import the c++ wrapper for matplot lib
// https://github.com/lava/matplotlib-cpp
// sudo apt-get install python-matplotlib python-numpy python2.7-dev
#include "plot/matplotlibcpp.h"

// Will plot the xy 3d position of the pose trajectories
void plot_xy_positions(const std::string &name, const std::string &color, const std::vector<Eigen::Matrix<double,7,1>> &poses) {

    // Paramters for our line
    std::map<std::string, std::string> params;
    params.insert({"label", name});
    params.insert({"linestyle", "-"});
    params.insert({"color", color});

    // Create vectors of our x and y axis
    std::vector<double> x, y;
    for(size_t i=0; i<poses.size(); i++) {
        x.push_back(poses.at(i)(0));
        y.push_back(poses.at(i)(1));
    }

    // Finally plot
    matplotlibcpp::plot(x, y, params);

}


// Will plot the z 3d position of the pose trajectories
void plot_z_positions(const std::string &name, const std::string &color, const std::vector<double> &times, const std::vector<Eigen::Matrix<double,7,1>> &poses) {

    // Paramters for our line
    std::map<std::string, std::string> params;
    params.insert({"label", name});
    params.insert({"linestyle", "-"});
    params.insert({"color", color});

    // Create vectors of our x and y axis
    std::vector<double> time, z;
    for(size_t i=0; i<poses.size(); i++) {
        time.push_back(times.at(i));
        z.push_back(poses.at(i)(2));
    }

    // Finally plot
    matplotlibcpp::plot(time, z, params);

}


#endif


int main(int argc, char **argv) {

    // Ensure we have a path
    if(argc < 3) {
        printf(RED "ERROR: Please specify a align mode and trajectory file\n" RESET);
        printf(RED "ERROR: ./plot_trajectories <align_mode> <file_gt.txt> <file_est1.txt> ...  <file_est9.txt>\n" RESET);
        printf(RED "ERROR: rosrun ov_eval plot_trajectories <align_mode> <file_gt.txt> <file_est1.txt> ...  <file_est9.txt>\n" RESET);
        std::exit(EXIT_FAILURE);
    }

    // Read in all our trajectories from file
    std::vector<std::string> names;
    std::vector<std::vector<double>> times;
    std::vector<std::vector<Eigen::Matrix<double,7,1>>> poses;
    for(int i=2; i<argc; i++) {

        // Read in trajectory data
        std::vector<double> times_temp;
        std::vector<Eigen::Matrix<double,7,1>> poses_temp;
        std::vector<Eigen::Matrix3d> cov_ori_temp, cov_pos_temp;
        ov_eval::Loader::load_data(argv[i], times_temp, poses_temp, cov_ori_temp, cov_pos_temp);

        // Align all the non-groundtruth trajectories to the base one
        if(i>2) {

            // Intersect timestamps
            std::vector<double> gt_times_temp(times.at(0));
            std::vector<Eigen::Matrix<double,7,1>> gt_poses_temp(poses.at(0));
            ov_eval::AlignUtils::perform_association(0, 0.02, times_temp, gt_times_temp, poses_temp, gt_poses_temp);

            // Return failure if we didn't have any common timestamps
            if(poses_temp.size() < 2) {
                printf(RED "[TRAJ]: unable to get enough common timestamps between trajectories.\n" RESET);
                printf(RED "[TRAJ]: does the estimated trajectory publish the rosbag timestamps??\n" RESET);
                std::exit(EXIT_FAILURE);
            }

            // Perform alignment of the trajectories
            Eigen::Matrix3d R_ESTtoGT;
            Eigen::Vector3d t_ESTinGT;
            double s_ESTtoGT;
            ov_eval::AlignTrajectory::align_trajectory(poses_temp, gt_poses_temp, R_ESTtoGT, t_ESTinGT, s_ESTtoGT, argv[1]);

            // Debug print to the user
            Eigen::Vector4d q_ESTtoGT = ov_eval::Math::rot_2_quat(R_ESTtoGT);
            printf("[TRAJ]: q_ESTtoGT = %.3f, %.3f, %.3f, %.3f | p_ESTinGT = %.3f, %.3f, %.3f | s = %.2f\n",q_ESTtoGT(0),q_ESTtoGT(1),q_ESTtoGT(2),q_ESTtoGT(3),t_ESTinGT(0),t_ESTinGT(1),t_ESTinGT(2),s_ESTtoGT);

            // Finally lets calculate the aligned trajectories
            std::vector<Eigen::Matrix<double,7,1>> est_poses_aignedtoGT;
            Eigen::Vector3d gt_est_origin=Eigen::Vector3d::Zero();
            string method=argv[1];
            cout<<method<<endl;
            for(size_t j=0; j<gt_times_temp.size(); j++) {
                Eigen::Matrix<double,7,1> pose_ESTinGT;
                if(int(j)==0 && (method=="posyaworigin"||method=="se3origin"))
                {
                    gt_est_origin=s_ESTtoGT*R_ESTtoGT*poses_temp.at(j).block(0,0,3,1)+t_ESTinGT;
                    cout<<"gt_est_origin: "<<gt_est_origin.transpose()<<endl;
                }
                // cout<<"gt_est_origin: "<<gt_est_origin.transpose()<<endl;
                pose_ESTinGT.block(0,0,3,1) = s_ESTtoGT*R_ESTtoGT*poses_temp.at(j).block(0,0,3,1)+t_ESTinGT-gt_est_origin;
        
                pose_ESTinGT.block(3,0,4,1) = ov_eval::Math::quat_multiply(poses_temp.at(j).block(3,0,4,1),ov_eval::Math::Inv(q_ESTtoGT));
                est_poses_aignedtoGT.push_back(pose_ESTinGT);
            }

            // Overwrite our poses with the corrected ones
            poses_temp = est_poses_aignedtoGT;

        }


        // Debug print the length stats
        boost::filesystem::path path(argv[i]);
        std::string name = path.stem().string();
        double length = ov_eval::Loader::get_total_length(poses_temp);
        printf("[COMP]: %d poses in %s => length of %.2f meters\n",(int)times_temp.size(),name.c_str(),length);

        // Save this to our arrays
        names.push_back(name);
        times.push_back(times_temp);
        poses.push_back(poses_temp);

    }


   for(size_t i=1; i<times.size();i++)
   {
     ofstream of;
     string name="/tmp/"+to_string(i)+"_traj.txt";
     std::cout<<"name: "<<name<<std::endl;
     if(boost::filesystem::exists(name))
      {
            boost::filesystem::remove(name);
            cout<<"exist output_file, remove."<<endl;
      }

     of.open(name,ofstream::out|ofstream::app);
     for(int j=0;j<times[i].size();j++)
     {
       of<<to_string(times[i][j])<<" "<<to_string(poses[i][j](0))<<" "<<to_string(poses[i][j](1))<<" "<<to_string(poses[i][j](2))<<
       " "<<to_string(poses[i][j](3))<<" "<<to_string(poses[i][j](4))<<" "<<to_string(poses[i][j](5))<<" "<<to_string(poses[i][j](6))<<endl;
     }
     of.close();
   }


#ifdef HAVE_PYTHONLIBS

    // Colors that we are plotting
    std::vector<std::string> colors = {"black","blue","red","green","cyan","magenta"};
    //assert(algo_rpe.size() <= colors.size()*linestyle.size());

    // Plot this figure
    matplotlibcpp::figure_size(1000, 750);

    // Plot the position trajectories
    for(size_t i=0; i<times.size(); i++) {
        plot_xy_positions(names.at(i), colors.at(i), poses.at(i));
    }

    // Display to the user
    matplotlibcpp::xlabel("x-axis (m)");
    matplotlibcpp::ylabel("y-axis (m)");
    matplotlibcpp::legend();
    matplotlibcpp::show(false);


    // Plot this figure
    matplotlibcpp::figure_size(1000, 350);

    // Zero our time arrays
    double starttime = (times.at(0).empty())? 0 : times.at(0).at(0);
    double endtime = (times.at(0).empty())? 0 : times.at(0).at(times.at(0).size()-1);
    for(size_t i=0; i<times.size(); i++) {
        for(size_t j=0; j<times.at(i).size(); j++) {
            times.at(i).at(j) -= starttime;
        }
    }

    // Plot the position trajectories
    for(size_t i=0; i<times.size(); i++) {
        plot_z_positions(names.at(i), colors.at(i), times.at(i), poses.at(i));
    }

    // Display to the user
    matplotlibcpp::xlabel("timestamp (sec)");
    matplotlibcpp::ylabel("z-axis (m)");
    matplotlibcpp::xlim(0.0,endtime-starttime);
    matplotlibcpp::legend();
    matplotlibcpp::show(true);


#endif


    // Done!
    return EXIT_SUCCESS;

}


