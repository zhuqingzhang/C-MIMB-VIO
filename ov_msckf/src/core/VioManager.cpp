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
#include "VioManager.h"
#include "types/Landmark.h"
#include <set>
#include<fstream>



VioManager::VioManager(VioManagerOptions& params_,Simulator* sim): simulator(sim){


    // Nice startup message
    printf("=======================================\n");
    printf("OPENVINS ON-MANIFOLD EKF IS STARTING\n");
    printf("=======================================\n");

    // Nice debug
    this->params = params_;
    params.print_estimator();
    params.print_noise();
    params.print_state();
    params.print_trackers();

    // Create the state!!
    state = new State(params.state_options);

    if(simulator!=nullptr&&state->_options.use_gt)
    {
      state->init_spline(simulator->traj_data); 
      state->set_points_gt(simulator->featmap);     
    }

    cout<<"finish State creation"<<endl;

    // Timeoffset from camera to IMU
    Eigen::VectorXd temp_camimu_dt;
    temp_camimu_dt.resize(1);
    temp_camimu_dt(0) = params.calib_camimu_dt;
    state->_calib_dt_CAMtoIMU->set_value(temp_camimu_dt);
    state->_calib_dt_CAMtoIMU->set_fej(temp_camimu_dt);
    state->_calib_dt_CAMtoIMU->set_linp(temp_camimu_dt);

    // Loop through through, and load each of the cameras
    for(int i=0; i<state->_options.num_cameras; i++) {

        // If our distortions are fisheye or not!
        state->_cam_intrinsics_model.at(i) = params.camera_fisheye.at(i);

        // Camera intrinsic properties
        state->_cam_intrinsics.at(i)->set_value(params.camera_intrinsics.at(i));
        state->_cam_intrinsics.at(i)->set_fej(params.camera_intrinsics.at(i));
        state->_cam_intrinsics.at(i)->set_linp(params.camera_intrinsics.at(i));

        // Our camera extrinsic transform
        state->_calib_IMUtoCAM.at(i)->set_value(params.camera_extrinsics.at(i));
        state->_calib_IMUtoCAM.at(i)->set_fej(params.camera_extrinsics.at(i));
        state->_calib_IMUtoCAM.at(i)->set_linp(params.camera_extrinsics.at(i));

    }

    //===================================================================================
    //===================================================================================
    //===================================================================================

    // If we are recording statistics, then open our file
    if(params.record_timing_information) {
        // If the file exists, then delete it
        if (boost::filesystem::exists(params.record_timing_filepath)) {
            boost::filesystem::remove(params.record_timing_filepath);
            printf(YELLOW "[STATS]: found old file found, deleted...\n" RESET);
        }
        // Create the directory that we will open the file in
        boost::filesystem::path p(params.record_timing_filepath);
        boost::filesystem::create_directories(p.parent_path());
        // Open our statistics file!
        of_statistics.open(params.record_timing_filepath, std::ofstream::out | std::ofstream::app);
        // Write the header information into it
        of_statistics << "# timestamp (sec),tracking,propagation,msckf update,";
        if(state->_options.max_slam_features > 0) {
            of_statistics << "slam update,slam delayed,";
        }
        of_statistics << "marginalization,total" << std::endl;
    }

    if(params.save_match_points) {
        // If the file exists, then delete it
        if (boost::filesystem::exists(params.points_filename)) {
            boost::filesystem::remove(params.points_filename);
            printf(YELLOW "[STATS]: found old file found, deleted...\n" RESET);
        }
        // Create the directory that we will open the file in
        boost::filesystem::path p(params.points_filename);
        boost::filesystem::create_directories(p.parent_path());
        // Open our statistics file!
        state->of_points.open(params.points_filename, std::ofstream::out | std::ofstream::app);

    }

    for(int i=0;i<params.num_map;i++)
    {
      string file_name="/tmp/keyframe_position_seq"+to_string(i)+".txt";
      if (boost::filesystem::exists(file_name)) {
            boost::filesystem::remove(file_name);
            printf(YELLOW "[STATS]: found old file found, deleted...\n" RESET);
      }
      boost::filesystem::path p(file_name);
      boost::filesystem::create_directories(p.parent_path());

      ofstream of;
      state->of_keyframe_positions.push_back(std::move(of));
        // Open our statistics file!
      state->of_keyframe_positions[i].open(file_name, std::ofstream::out | std::ofstream::app);

    }

    if(params.save_transform) {
        // If the file exists, then delete it
        if (boost::filesystem::exists(params.transform_filename)) {
            boost::filesystem::remove(params.transform_filename);
            printf(YELLOW "[STATS]: found old file found, deleted...\n" RESET);
        }
        // Create the directory that we will open the file in
        boost::filesystem::path p(params.transform_filename);
        boost::filesystem::create_directories(p.parent_path());
        // Open our statistics file!
        of_transform.open(params.transform_filename, std::ofstream::out | std::ofstream::app);

    }




    //===================================================================================
    //===================================================================================
    //===================================================================================


    // Lets make a feature extractor
    if(params.use_klt) {
        trackFEATS = new TrackKLT(params.num_pts,state->_options.max_aruco_features,params.fast_threshold,params.grid_x,params.grid_y,params.min_px_dist);
        trackFEATS->set_calibration(params.camera_intrinsics, params.camera_fisheye);
    } else {
        trackFEATS = new TrackDescriptor(params.num_pts,state->_options.max_aruco_features,params.fast_threshold,params.grid_x,params.grid_y,params.knn_ratio);
        trackFEATS->set_calibration(params.camera_intrinsics, params.camera_fisheye);
    }

    // Initialize our aruco tag extractor
    if(params.use_aruco) {
        trackARUCO = new TrackAruco(state->_options.max_aruco_features, params.downsize_aruco);
        trackARUCO->set_calibration(params.camera_intrinsics, params.camera_fisheye);
    }

    // Initialize our matcher to do kf match
    cout<<"// Initialize our matcher to do kf match"<<endl;
    if(params.use_robust_match)
    {
        matchKF = new MatchRobust(params.match_base_options);
    }
    else
    {
        if(MapFeatureType::Type::Brief==MapFeatureType::from_string(params.match_base_options.map_feature_type))
        {
            matchKF = new MatchBrief(params.match_base_options);
        }
        else if(MapFeatureType::Type::ORB==MapFeatureType::from_string(params.match_base_options.map_feature_type))
        {
            matchKF = new MatchORB(params.match_base_options);
        }
        else if(MapFeatureType::Type::UNKNOWN==MapFeatureType::from_string(params.match_base_options.map_feature_type))
        {
            printf(RED "Wrong map feature type" RESET);
            std::exit(EXIT_FAILURE);
        }
        state->kfdatabase = matchKF->get_interval_kfdatabase();
        th_matchKF = new thread(&ov_core::MatchBase::DetectAndMatch, matchKF);
    }
    cout<<"// finsih Initialize our matcher to do kf match"<<endl;

    // Initialize our state propagator
    propagator = new Propagator(params.imu_noises, params.gravity);

    // Our state initialize
    initializer = new InertialInitializer(params.gravity,params.init_window_time,params.init_imu_thresh);

    // Make the updater!
    updaterMSCKF = new UpdaterMSCKF(params.msckf_options,params.featinit_options);
    updaterSLAM = new UpdaterSLAM(params.slam_options,params.aruco_options,params.featinit_options);

    updaterOptimize = new UpdaterOptimize();


    // If we are using zero velocity updates, then create the updater
    if(params.try_zupt) {
        updaterZUPT = new UpdaterZeroVelocity(params.zupt_options,params.imu_noises,params.gravity,params.zupt_max_velocity,params.zupt_noise_multiplier);
    }

}




void VioManager::feed_measurement_imu(double timestamp, Eigen::Vector3d wm, Eigen::Vector3d am) {

    // Push back to our propagator
    //Store imu message into imu_data of propagator.
    // Only store the last 20s of imu message. by zzq
    propagator->feed_imu(timestamp,wm,am);

    // Push back to our initializer
    //
    if(!is_initialized_vio) {
        //Store imu message into imu_data of propagator.
        // Only store the last three of our initialization windows time of imu message. by zzq
        initializer->feed_imu(timestamp, wm, am);
    }

    // Push back to the zero velocity updater if we have it
    if(updaterZUPT != nullptr) {
        //Store imu message into imu_data of propagator.
        // Only store the last 60s of imu message. by zzq
        updaterZUPT->feed_imu(timestamp, wm, am);
    }

}





void VioManager::feed_measurement_monocular(double timestamp, cv::Mat& img0, size_t cam_id) {

    // Start timing
    rT1 =  boost::posix_time::microsec_clock::local_time();

    // Downsample if we are downsampling
    if(params.downsample_cameras) {
        cv::Mat img0_temp;
        cv::pyrDown(img0,img0_temp,cv::Size(img0.cols/2.0,img0.rows/2.0));
        img0 = img0_temp.clone();
    }

    // Check if we should do zero-velocity, if so update the state with it
    if(is_initialized_vio && updaterZUPT != nullptr) {
        did_zupt_update = updaterZUPT->try_update(state, timestamp);
        if(did_zupt_update) {
            cv::Mat img_outtemp0;
            cv::cvtColor(img0, img_outtemp0, CV_GRAY2RGB);
            bool is_small = (std::min(img0.cols,img0.rows) < 400);
            auto txtpt = (is_small)? cv::Point(10,30) : cv::Point(30,60);
            cv::putText(img_outtemp0, "zvup active", txtpt, cv::FONT_HERSHEY_COMPLEX_SMALL, (is_small)? 1.0 : 2.0, cv::Scalar(0,0,255),3);
            zupt_image = img_outtemp0.clone();
            return;
        }
    }

    // Feed our trackers
    //For KLTtracker, first histogram equalize,then build image pyramid,then extract more features on the last step image, then using klt to do feature matching.
    //For DescriptorTracker, first histogram equalize, then extract fast feature and orb descriptor and perform matching between stereo images(if it is stereo), then perform matching to track features.
    trackFEATS->feed_monocular(timestamp, img0, cam_id);

    // If aruoc is avalible, the also pass to it
    if(trackARUCO != nullptr) {
        trackARUCO->feed_monocular(timestamp, img0, cam_id);
    }
    rT2 =  boost::posix_time::microsec_clock::local_time();

    // If we do not have VIO initialization, then try to initialize
    // TODO: Or if we are trying to reset the system, then do that here!
    if(!is_initialized_vio) {
        is_initialized_vio = try_to_initialize();
        if(!is_initialized_vio) return;
    }


    Keyframe *cur_kf= nullptr;
    //we only construct kf when there are enough clones
    //otherwise it is no use to waste time to construct it
    //as in do_feature_propagate_update, the system would just prop and return
    if(params.use_prior_map && (int)state->_clones_IMU.size() >= std::min(state->_options.max_clone_size,4)) {
        if (last_kf_match_time == -1 || timestamp - last_kf_match_time >= params.kf_match_interval) {
            //get the current images features
            cout<<"in cur_kf construction"<<endl;

            //first, we need to construct current images as Keyframe type for future convienience
            size_t index_0 = max_kf_id;
            max_kf_id++;
            cur_kf=new Keyframe(timestamp,index_0,cam_id,img0,params.camera_intrinsics[cam_id]);

        }
    }


    // Call on our propagate and update function
    do_feature_propagate_update(timestamp,cur_kf);

    delete cur_kf;


}

void VioManager::feed_measurement_stereo(double timestamp, cv::Mat& img0, cv::Mat& img1, size_t cam_id0, size_t cam_id1) {

    // Start timing
    rT1 =  boost::posix_time::microsec_clock::local_time();

    // Assert we have good ids
    // cout<<"feed stereo 1"<<endl;
    assert(cam_id0!=cam_id1);

    // Downsample if we are downsampling
    if(params.downsample_cameras) {
        cv::Mat img0_temp, img1_temp;
        cv::pyrDown(img0,img0_temp,cv::Size(img0.cols/2.0,img0.rows/2.0));
        cv::pyrDown(img1,img1_temp,cv::Size(img1.cols/2.0,img1.rows/2.0));
        img0 = img0_temp.clone();
        img1 = img1_temp.clone();
        cout<<"downsample_cameras"<<endl;
    }
    // cout<<"feed stereo 2"<<endl;
    // Check if we should do zero-velocity, if so update the state with it
    
    if(is_initialized_vio && updaterZUPT != nullptr) {
        did_zupt_update = updaterZUPT->try_update(state, timestamp);
        if(did_zupt_update) {
            cout<<"did zupt update1"<<endl;
            cv::Mat img_outtemp0, img_outtemp1;
            cv::cvtColor(img0, img_outtemp0, CV_GRAY2RGB);
            cv::cvtColor(img1, img_outtemp1, CV_GRAY2RGB);
            cout<<"did zupt update2"<<endl;
            bool is_small = (std::min(img0.cols,img0.rows) < 400);
            auto txtpt = (is_small)? cv::Point(10,30) : cv::Point(30,60);
            cv::putText(img_outtemp0, "zvup active", txtpt, cv::FONT_HERSHEY_COMPLEX_SMALL, (is_small)? 1.0 : 2.0, cv::Scalar(0,0,255),3);
            cv::putText(img_outtemp1, "zvup active", txtpt, cv::FONT_HERSHEY_COMPLEX_SMALL, (is_small)? 1.0 : 2.0, cv::Scalar(0,0,255),3);
            cv::hconcat(img_outtemp0, img_outtemp1, zupt_image);
            cout<<"did zupt update3"<<endl;
            stop_time.push_back(timestamp);
            cout<<"did zupt updat4"<<endl;
            return;
        }
    }
    // cout<<"feed stereo 3"<<endl;

    if(map_initialized&&!stop_time.empty())
    {
        double duration=stop_time.back()-stop_time.front();
        last_kf_match_time+=duration;
        
    }
    stop_time.clear();
    // cout<<"feed stereo 4"<<endl;

        //* feed the image to MatchBase for detect map matching
    Keyframe *cur_kf_0= nullptr;
    Keyframe *cur_kf_1= nullptr;
    //we only construct kf when there are enough clones
    //otherwise it is no use to waste time to construct it
    //as in do_feature_propagate_update, the system would just prop and return
   if(params.use_prior_map && (int)state->_clones_IMU.size() >= std::min(state->_options.max_clone_size,5)) {
    //    if (last_kf_match_time == -1 || timestamp - last_kf_match_time >= params.kf_match_interval) 
       {
           //get the current images features
          cout<<"in cur_kf construction"<<endl;

           //first, we need to construct current images as Keyframe type for future convienience
           max_kf_id++;
           size_t index_0 = max_kf_id;
           cur_kf_0=new Keyframe(timestamp,index_0,cam_id0,img0,params.camera_intrinsics[cam_id0]);
           Keyframe kf_0(timestamp,index_0,cam_id0,img0,params.camera_intrinsics[cam_id0]);

           //TODO:maybe we used the left image is ok, the right image is no use?
           max_kf_id++;
           size_t index_1 = max_kf_id;
           cur_kf_1=new Keyframe(timestamp,index_1,cam_id1,img1,params.camera_intrinsics[cam_id1]);
          //  Keyframe kf_1(timestamp,index_1,cam_id1,img1,params.camera_intrinsics[cam_id1]);

          cout<<"finish cur_kf_1"<<endl;
          if(!params.use_robust_match)
          {
            matchKF->feed_image(kf_0);
          }
          
       }
   }

    // Feed our stereo trackers, if we are not doing binocular
    if(params.use_stereo) {
        trackFEATS->feed_stereo(timestamp, img0, img1, cam_id0, cam_id1);
        cout<<"trackFEATS->feed_stereo"<<endl;
    } else {
        boost::thread t_l = boost::thread(&TrackBase::feed_monocular, trackFEATS, boost::ref(timestamp), boost::ref(img0), boost::ref(cam_id0));
        boost::thread t_r = boost::thread(&TrackBase::feed_monocular, trackFEATS, boost::ref(timestamp), boost::ref(img1), boost::ref(cam_id1));
        t_l.join();
        t_r.join();
    }
    // cout<<"feed stereo 5"<<endl;

    // If aruoc is avalible, the also pass to it
    // NOTE: binocular tracking for aruco doesn't make sense as we by default have the ids
    // NOTE: thus we just call the stereo tracking if we are doing binocular!
    if(trackARUCO != nullptr) {
        trackARUCO->feed_stereo(timestamp, img0, img1, cam_id0, cam_id1);
    }
    rT2 =  boost::posix_time::microsec_clock::local_time();

    // If we do not have VIO initialization, then try to initialize
    // TODO: Or if we are trying to reset the system, then do that here!
    if(!is_initialized_vio) {
        is_initialized_vio = try_to_initialize();
        cout<<"is_initialized_vio: "<<is_initialized_vio<<endl;
        if(!is_initialized_vio) return;
    }

    // Call on our propagate and update function
    do_feature_propagate_update(timestamp,cur_kf_0,cur_kf_1);

   delete cur_kf_0;
   delete cur_kf_1;

}



void VioManager::feed_measurement_simulation(double timestamp, const std::vector<int> &camids, const std::vector<std::vector<std::pair<size_t,Eigen::VectorXf>>> &feats) {

    // Start timing
    rT1 =  boost::posix_time::microsec_clock::local_time();

    // Check if we actually have a simulated tracker
    TrackSIM *trackSIM = dynamic_cast<TrackSIM*>(trackFEATS);
    if(trackSIM == nullptr) {
        //delete trackFEATS; //(fix this error in the future)
        trackFEATS = new TrackSIM(state->_options.max_aruco_features);
        trackFEATS->set_calibration(params.camera_intrinsics, params.camera_fisheye);
        trackFEATS->set_sim_feats_num(sim_feats_num);
        printf(RED "[SIM]: casting our tracker to a TrackSIM object!\n" RESET);
    }

    // Check if we should do zero-velocity, if so update the state with it
    if(is_initialized_vio && updaterZUPT != nullptr) {
        did_zupt_update = updaterZUPT->try_update(state, timestamp);
        if(did_zupt_update) {
            int max_width = -1;
            int max_height = -1;
            for(auto &pair : params.camera_wh) {
                if(max_width < pair.second.first) max_width = pair.second.first;
                if(max_height < pair.second.second) max_height = pair.second.second;
            }
            for(int n=0; n<params.state_options.num_cameras; n++) {
                cv::Mat img_outtemp0 = cv::Mat::zeros(cv::Size(max_width,max_height), CV_8UC3);
                bool is_small = (std::min(img_outtemp0.cols,img_outtemp0.rows) < 400);
                auto txtpt = (is_small)? cv::Point(10,30) : cv::Point(30,60);
                cv::putText(img_outtemp0, "zvup active", txtpt, cv::FONT_HERSHEY_COMPLEX_SMALL, (is_small)? 1.0 : 2.0, cv::Scalar(0,0,255),3);
                if(n == 0) {
                    zupt_image = img_outtemp0.clone();
                } else {
                    cv::hconcat(zupt_image, img_outtemp0, zupt_image);
                }
            }
            return;
        }
    }

    // Cast the tracker to our simulation tracker
    trackSIM = dynamic_cast<TrackSIM*>(trackFEATS);
    trackSIM->set_width_height(params.camera_wh);
   

    // Feed our simulation tracker
    trackSIM->feed_measurement_simulation(timestamp, camids, feats);
    rT2 =  boost::posix_time::microsec_clock::local_time();

    // If we do not have VIO initialization, then return an error
    if(!is_initialized_vio) {
        printf(RED "[SIM]: your vio system should already be initialized before simulating features!!!\n" RESET);
        printf(RED "[SIM]: initialize your system first before calling feed_measurement_simulation()!!!!\n" RESET);
        std::exit(EXIT_FAILURE);
    }
    
    Keyframe *cur_kf_0= nullptr;
    Keyframe *cur_kf_1= nullptr;
    if(params.use_prior_map && (int)state->_clones_IMU.size() >= std::min(state->_options.max_clone_size,4)) {
    //    if (last_kf_match_time == -1 || timestamp - last_kf_match_time >= params.kf_match_interval) 
       {
           //get the current images features
          cout<<"in cur_kf construction"<<endl;
//           vector<cv::Point2f> uv_0, uv_1;
//           vector<cv::Point2f> uv_norm_0, uv_norm_1;
//           vector<size_t> feature_id_0, feature_id_1;
//           get_image_features(timestamp, cam_id0, uv_0, uv_norm_0, feature_id_0);
//           get_image_features(timestamp, cam_id1, uv_1, uv_norm_1, feature_id_1);
//
//           cout<<"after get features"<<endl;

           //first, we need to construct current images as Keyframe type for future convienience
           size_t index_0 = max_kf_id;
           max_kf_id++;
           cv::Mat img0;
           cur_kf_0=new Keyframe(timestamp,index_0,camids[0],img0,params.camera_intrinsics[camids[0]]);


           if(camids.size()>1)
           {
            cv::Mat img1;
            size_t index_1 = max_kf_id;
            max_kf_id++;
            cur_kf_1=new Keyframe(timestamp,index_1,camids[1],img1,params.camera_intrinsics[camids[1]]);

            cout<<"finish cur_kf_1"<<endl;
           }

       }
   }

    // Call on our propagate and update function
    do_feature_propagate_update(timestamp,cur_kf_0,cur_kf_1);

    delete cur_kf_0;
    delete cur_kf_1;


}


bool VioManager::try_to_initialize() {

    // Returns from our initializer

    double time0;
    Eigen::Matrix<double, 4, 1> q_GtoI0;
    Eigen::Matrix<double, 3, 1> b_w0, v_I0inG, b_a0, p_I0inG;

    // Try to initialize the system
    // We will wait for a jerk if we do not have the zero velocity update enabled
    // Otherwise we can initialize right away as the zero velocity will handle the stationary case
    bool wait_for_jerk = (updaterZUPT == nullptr);
    bool success = initializer->initialize_with_imu(time0, q_GtoI0, b_w0, v_I0inG, b_a0, p_I0inG, wait_for_jerk);

    // Return if it failed
    if (!success) {
        return false;
    }

    // Make big vector (q,p,v,bg,ba), and update our state
    // Note: start from zero position, as this is what our covariance is based off of
    // Frome initializa_with_imu, v_I0inG and p_I0inG are zeros. by zzq
    Eigen::Matrix<double,16,1> imu_val;
    imu_val.block(0,0,4,1) = q_GtoI0;
    imu_val.block(4,0,3,1) << 0,0,0;
    imu_val.block(7,0,3,1) = v_I0inG;
    imu_val.block(10,0,3,1) = b_w0;
    imu_val.block(13,0,3,1) = b_a0;
    //imu_val.block(10,0,3,1) << 0,0,0;
    //imu_val.block(13,0,3,1) << 0,0,0;
    state->_imu->set_value(imu_val);
    state->_imu->set_fej(imu_val);
    state->_timestamp = time0;
    startup_time = time0;

    // Cleanup any features older then the initialization time
    trackFEATS->get_feature_database()->cleanup_measurements(state->_timestamp);
    if(trackARUCO != nullptr) {
        trackARUCO->get_feature_database()->cleanup_measurements(state->_timestamp);
    }

    // Else we are good to go, print out our stats
    printf(GREEN "[INIT]: orientation = %.4f, %.4f, %.4f, %.4f\n" RESET,state->_imu->quat()(0),state->_imu->quat()(1),state->_imu->quat()(2),state->_imu->quat()(3));
    printf(GREEN "[INIT]: bias gyro = %.4f, %.4f, %.4f\n" RESET,state->_imu->bias_g()(0),state->_imu->bias_g()(1),state->_imu->bias_g()(2));
    printf(GREEN "[INIT]: velocity = %.4f, %.4f, %.4f\n" RESET,state->_imu->vel()(0),state->_imu->vel()(1),state->_imu->vel()(2));
    printf(GREEN "[INIT]: bias accel = %.4f, %.4f, %.4f\n" RESET,state->_imu->bias_a()(0),state->_imu->bias_a()(1),state->_imu->bias_a()(2));
    printf(GREEN "[INIT]: position = %.4f, %.4f, %.4f\n" RESET,state->_imu->pos()(0),state->_imu->pos()(1),state->_imu->pos()(2));
    return true;

}



void VioManager::do_feature_propagate_update(double timestamp, Keyframe* cur_kf_0, Keyframe* cur_kf_1) {


    //===================================================================================
    // State propagation, and clone augmentation
    //===================================================================================

    // Return if the camera measurement is out of order
    if(state->_timestamp >= timestamp) {
        printf(YELLOW "image received out of order (prop dt = %3f)\n" RESET,(timestamp-state->_timestamp));
        return;
    }

    // Propagate the state forward to the current update time
    // Also augment it with a new clone!
    //Here just augment, no implementation about remove old state. by zzq
    cout<<"before propagate_and_clone"<<endl;
    cout<<"state timestamp: "<<to_string(state->_timestamp)<<" imu orientation: "<<state->_imu->quat().transpose()<<" imu position: "<<state->_imu->pos().transpose()<<endl;
    propagator->propagate_and_clone(state, timestamp);
    rT3 =  boost::posix_time::microsec_clock::local_time();
    cout<<"after propagate and clone"<<endl;
    cout<<"state timestamp: "<<to_string(state->_timestamp)<<" imu orientation: "<<state->_imu->quat().transpose()<<" imu position: "<<state->_imu->pos().transpose()<<endl;
    
    // If we have not reached max clones, we should just return...
    // This isn't super ideal, but it keeps the logic after this easier...
    // We can start processing things when we have at least 5 clones since we can start triangulating things...
    if((int)state->_clones_IMU.size() < std::min(state->_options.max_clone_size,5)) {
        printf("waiting for enough clone states (%d of %d)....\n",(int)state->_clones_IMU.size(),std::min(state->_options.max_clone_size,5));
        return;
    }

    // Return if we where unable to propagate
    //Note, In propagate_and_clone above, after EKFpropagation, state->timestamp is equal to timestamp
    //so, here, if state->_timestamp!=timestamp, there is error
    if(state->_timestamp != timestamp) {
        printf(RED "[PROP]: Propagator unable to propagate the state forward in time!\n" RESET);
        printf(RED "[PROP]: It has been %.3f since last time we propagated\n" RESET,timestamp-state->_timestamp);
        return;
    }

    
    //===================================================================================
    // MSCKF features and KLT tracks that are SLAM features
    //===================================================================================


    // Now, lets get all features that should be used for an update that are lost in the newest frame
    std::vector<Feature*> feats_lost,feats_kf_linked, feats_marg, feats_slam;
    feats_lost = trackFEATS->get_feature_database()->features_not_containing_newer(state->_timestamp);

    cout<<"feats_lost"<<endl;


    // Don't need to get the oldest features untill we reach our max number of clones
    if((int)state->_clones_IMU.size() > state->_options.max_clone_size) {
        feats_marg = trackFEATS->get_feature_database()->features_containing(state->margtimestep());
        if(trackARUCO != nullptr && timestamp-startup_time >= params.dt_slam_delay) {
            feats_slam = trackARUCO->get_feature_database()->features_containing(state->margtimestep());
        }
    }

    // We also need to make sure that the max tracks does not contain any lost features
    // This could happen if the feature was lost in the newest frame, but has a measurement at the marg timestep
    auto it1 = feats_lost.begin();
    while(it1 != feats_lost.end()) {
        if(std::find(feats_marg.begin(),feats_marg.end(),(*it1)) != feats_marg.end()) {
            //printf(YELLOW "FOUND FEATURE THAT WAS IN BOTH feats_lost and feats_marg!!!!!!\n" RESET);
            it1 = feats_lost.erase(it1);
        } else {
            it1++;
        }
    }

    // Find tracks that have reached max length, these can be made into SLAM features
    std::vector<Feature*> feats_maxtracks;
    auto it2 = feats_marg.begin();
    while(it2 != feats_marg.end()) {
        // See if any of our camera's reached max track
        bool reached_max = false;
        for (const auto &cams: (*it2)->timestamps) {
            if ((int)cams.second.size() > state->_options.max_clone_size) {
                reached_max = true;
                break;
            }
        }
        // If max track, then add it to our possible slam feature list
        if(reached_max) {
            feats_maxtracks.push_back(*it2);
            it2 = feats_marg.erase(it2);
        } else {
            it2++;
        }
    }

    // Count how many aruco tags we have in our state
    int curr_aruco_tags = 0;
    auto it0 = state->_features_SLAM.begin();
    while(it0 != state->_features_SLAM.end()) {
        if ((int) (*it0).second->_featid <= state->_options.max_aruco_features) curr_aruco_tags++;
        it0++;
    }

    // Append a new SLAM feature if we have the room to do so
    // Also check that we have waited our delay amount (normally prevents bad first set of slam points)
    if(state->_options.max_slam_features > 0 && timestamp-startup_time >= params.dt_slam_delay && (int)state->_features_SLAM.size() < state->_options.max_slam_features+curr_aruco_tags) {
        // Get the total amount to add, then the max amount that we can add given our marginalize feature array
        int amount_to_add = (state->_options.max_slam_features+curr_aruco_tags)-(int)state->_features_SLAM.size();
        int valid_amount = (amount_to_add > (int)feats_maxtracks.size())? (int)feats_maxtracks.size() : amount_to_add;
        // If we have at least 1 that we can add, lets add it!
        // Note: we remove them from the feat_marg(maybe it should be feats_maxtracks. by zzq) array since we don't want to reuse information...
        if(valid_amount > 0) {
            feats_slam.insert(feats_slam.end(), feats_maxtracks.end()-valid_amount, feats_maxtracks.end());
            feats_maxtracks.erase(feats_maxtracks.end()-valid_amount, feats_maxtracks.end());
        }
    }

    // Loop through current SLAM features, we have tracks of them, grab them for this update!
    // Note: if we have a slam feature that has lost tracking, then we should marginalize it out
    // Note: if you do not use FEJ, these types of slam features *degrade* the estimator performance....
    for (std::pair<const size_t, Landmark*> &landmark : state->_features_SLAM) {
        if(trackARUCO != nullptr) {
            Feature* feat1 = trackARUCO->get_feature_database()->get_feature(landmark.second->_featid);
            if(feat1 != nullptr) feats_slam.push_back(feat1);
        }
        Feature* feat2 = trackFEATS->get_feature_database()->get_feature(landmark.second->_featid);
        if(feat2 != nullptr) feats_slam.push_back(feat2);
        if(feat2 == nullptr) landmark.second->should_marg = true;
    }
    //Now,feats_slam contains all features that from both state->_features_SLAM and feats_maxtracks

    // Lets marginalize out all old SLAM features here (if SLAM features are in state->variables)
    // These are ones that where not successfully tracked into the current frame(i.e. have their marginalization flag set)
    // We do *NOT* marginalize out our aruco tags
    // Just delete the slam_feauture that are set should_marg from _features_SLAM,
    // and remove it from state->variables and resize the covariance.
    StateHelper::marginalize_slam(state);

    // Separate our SLAM features into new ones, and old ones
    std::vector<Feature*> feats_slam_DELAYED, feats_slam_UPDATE;
    for(size_t i=0; i<feats_slam.size(); i++) {
        if(state->_features_SLAM.find(feats_slam.at(i)->featid) != state->_features_SLAM.end()) {
            feats_slam_UPDATE.push_back(feats_slam.at(i));
            //printf("[UPDATE-SLAM]: found old feature %d (%d measurements)\n",(int)feats_slam.at(i)->featid,(int)feats_slam.at(i)->timestamps_left.size());
        } else {
            feats_slam_DELAYED.push_back(feats_slam.at(i));
            //printf("[UPDATE-SLAM]: new feature ready %d (%d measurements)\n",(int)feats_slam.at(i)->featid,(int)feats_slam.at(i)->timestamps_left.size());
        }
    }

    // Concatenate our MSCKF feature arrays (i.e., ones not being used for slam updates)
    //包含 当前帧没有追踪上的点， 最老帧观测到的点，以及那些track轨迹足够长本可以作为slam_feature但是由于空间限制没有加入到slam_feature中的点
    std::vector<Feature*> featsup_MSCKF = feats_lost;
    featsup_MSCKF.insert(featsup_MSCKF.end(), feats_marg.begin(), feats_marg.end());
    featsup_MSCKF.insert(featsup_MSCKF.end(), feats_maxtracks.begin(), feats_maxtracks.end());
    cout<<"featsup_MSCKF"<<endl;
    for(int i=0;i<featsup_MSCKF.size();i++)
    {
        assert(featsup_MSCKF[i]->new_extracted_map_match==false);
    }
    
    //===================================================================================
    // Now that we have a list of features, lets do the EKF update for MSCKF and SLAM!
    //===================================================================================


    // Pass them to our MSCKF updater
    // NOTE: if we have more then the max, we select the "best" ones (i.e. max tracks) for this update
    // NOTE: this should only really be used if you want to track a lot of features, or have limited computational resources
    if((int)featsup_MSCKF.size() > state->_options.max_msckf_in_update)
        featsup_MSCKF.erase(featsup_MSCKF.begin(), featsup_MSCKF.end()-state->_options.max_msckf_in_update);
    if(params.use_prior_map && state->_options.use_schmidt) //when we have nuisance part, we should do Schmidt-KF update
    {
        updaterMSCKF->update_skf(state, featsup_MSCKF);
        cout<<"updateMSCKF"<<endl;
    }
    else
    {
        updaterMSCKF->update(state,featsup_MSCKF);
        cout<<"updateMSCKF"<<endl;
    }
    rT4 =  boost::posix_time::microsec_clock::local_time();


    // Perform SLAM delay init and update
    // NOTE: that we provide the option here to do a *sequential* update
    // NOTE: this will be a lot faster but won't be as accurate.
    std::vector<Feature*> feats_slam_UPDATE_TEMP;
    while(!feats_slam_UPDATE.empty()) {
        // Get sub vector of the features we will update with
        std::vector<Feature*> featsup_TEMP;
        featsup_TEMP.insert(featsup_TEMP.begin(), feats_slam_UPDATE.begin(), feats_slam_UPDATE.begin()+std::min(state->_options.max_slam_in_update,(int)feats_slam_UPDATE.size()));
        feats_slam_UPDATE.erase(feats_slam_UPDATE.begin(), feats_slam_UPDATE.begin()+std::min(state->_options.max_slam_in_update,(int)feats_slam_UPDATE.size()));
        std::vector<Feature*> feats_slam_kf_linked;
        feats_slam_kf_linked.clear();
        // Do the update
        //unlike msckfupdate (project Hx to the nullspace of Hf), it make an aumgent Jacobian(H=[Hx,Hf]);
        if(params.use_prior_map&&state->_options.use_schmidt)
        {
            cout<<"before updaterSLAM->update_skf"<<endl;
            cout<<"featsup_TEMP size:"<<featsup_TEMP.size()<<endl;
            updaterSLAM->update_skf(state,featsup_TEMP);
            cout<<"after updaterSLAM->update_skf"<<endl;
            cout<<"feats_slam_kf_linked size: "<<feats_slam_kf_linked.size()<<endl;

        }
        else
        {
            cout<<"before updaterSLAM->update"<<endl;
            updaterSLAM->update(state, featsup_TEMP);
            cout<<"after updaterSLAM->update"<<endl;
        }

        feats_slam_UPDATE_TEMP.insert(feats_slam_UPDATE_TEMP.end(), featsup_TEMP.begin(), featsup_TEMP.end());
        // feats_slam_UPDATE_TEMP.insert(feats_slam_UPDATE_TEMP.end(),feats_slam_kf_linked.begin(),feats_slam_kf_linked.end());
    }
    feats_slam_UPDATE = feats_slam_UPDATE_TEMP;
    cout<<"finish slam update"<<endl;
    rT5 =  boost::posix_time::microsec_clock::local_time();
    //do the delayed_initial for new landmark, which compute the landmark's covariance
    //and add this landmark into state, and augment covariance, and update the state.
    std::vector<Feature*> feats_slam_kf_linked;
    feats_slam_kf_linked.clear();
    if(params.use_prior_map&&state->_options.use_schmidt)
    {
        //do feature update using feats_slam without keyframe_constrains
        cout<<"before updaterSLAM->udelayed_init_skf"<<endl;
        updaterSLAM->delayed_init_skf(state,feats_slam_DELAYED);
        cout<<"after updaterSLAM->udelayed_init_skf"<<endl;
        
    }
    else
    {
        cout<<"before updaterSLAM->delayinit"<<endl;
        updaterSLAM->delayed_init(state, feats_slam_DELAYED);
        cout<<"after updaterSLAM->delayinit"<<endl;
    }
   cout<<"finish slam delay update"<<endl;
    rT6 =  boost::posix_time::microsec_clock::local_time();

    //===================================================================================
    //Get map based information
    //===================================================================================
    vector<Feature*> feats_with_loop;
    vector<cv::Point2f> uv_loop,uv_norm;
    vector<cv::Point3f> uv_3d_loop;
    map<int,vector<cv::Point2f>> uv_loops,uv_norms;
    map<int,vector<cv::Point3f>> uv_3d_loops;
    vector<Keyframe*> loop_kfs;
    int index_kf=-1;
    bool do_map_kf_update=false;
    
    if(params.use_prior_map)
    {
        if(params.use_stereo && cur_kf_0!= nullptr && cur_kf_1!= nullptr)
        {
            //query the keyframe database and match with current frame.
            //1.traveser kfdb to find if current kf has the matched kf
            loop_kfs=get_loop_kfs(state);
            cout<<"out get_loop_kfs"<<endl;

            if(state->_clones_IMU.size()>state->_options.max_clone_size)
            {
                //we should check if delay_clones contained the margtimestep, if so, we need to do map_kf update
              cout<<"sliding windows is full"<<endl;
              if(!state->delay_clones.empty())
              {
                  assert(state->margtimestep()<=state->delay_clones[0]);
                  if(state->margtimestep()==state->delay_clones[0])
                  {
                      do_map_kf_update=true;
                      cout<<"!!!triger margin delay!!!"<<endl;
                      // sleep(2);
                  }
              }
            }

            if(!loop_kfs.empty())
            {
                //2.do feature link
                
              int i=-1;

              get_multi_map_KF_match_feature(timestamp,loop_kfs,index_kf,uv_loops,uv_3d_loops,uv_norms,feats_with_loop);

              for(int i=0;i<loop_kfs.size();i++)
              {
                StateHelper::add_kf_nuisance(state,loop_kfs[i]);
              }
              if(index_kf!=-1)
              {
                int seq = loop_kfs[index_kf]->sequence;
                uv_norm=uv_norms[seq];
                uv_3d_loop=uv_3d_loops[seq];
                uv_loops.clear();
                uv_3d_loops.clear();
                uv_norms.clear();
              }

              auto iter=uv_loops.begin();
              while(iter!=uv_loops.end())
              {
                int seq_id=iter->first;
                assert(state->set_transforms[seq_id]==false);

                Keyframe* loop_kf=nullptr;
                for(int i=0;i<loop_kfs.size();i++)
                {
                  if(loop_kfs[i]->sequence==seq_id)
                  {
                    loop_kf=loop_kfs[i];
                    break;
                  }
                }
                Vector3d p_loopIncur;
                Matrix3d R_loopTocur;

                if(loop_kf->PnPRANSAC(uv_norms[seq_id],uv_3d_loops[seq_id],p_loopIncur,R_loopTocur,params.match_num_thred))
                {
                  if(ComputeRelativePose(cur_kf_0,loop_kf,p_loopIncur,R_loopTocur))
                  {
                      // if(loop_kfs.size()>1)
                      // {
                      //     updaterOptimize->Optimize_initial_transform(state,loop_kfs);
                      // }
                      
                      last_kf_match_time=timestamp;
                      state->last_kf_match_position=state->_imu->pos();
                      //add the loop_kf into state nuisance part
                      
                      cout<<"before update initial transform"<<endl;
                      //  sleep(10);
                      // updaterMSCKF->update_initial_transform(state,feats_with_loop);

                      do_map_kf_update=false;
                      //
                      std::cout<<"============================map sequence "<<seq_id<<" is initialized!====================="<<endl;
                      cout<<" p_loopinCur: "<<p_loopIncur.transpose()<<endl;
                  }
                }
                

                iter++;
              }

              if(!feats_with_loop.empty())
              {
                state->delay_clones.push_back(timestamp);
            
                state->delay_loop_features.push_back(feats_with_loop);

                do_map_kf_update=true;
              }

                
                
            }
        }
        else if(!params.use_stereo && cur_kf_0!= nullptr)
        {
            //mono implementation
        }
        else
        {
            std::cout<<"there is something wrong with keyframe construction, no kf contrains added. Continue..."<<endl;
            return;
        }
    }
    //===================================================================================
    //Update with map based information
    //===================================================================================

    state->have_match=false;
    if(params.use_prior_map &&map_initialized&& do_map_kf_update)
    {
        state->have_match=true;
        vector<Feature*> loop_features;
        for(int i=0;i<state->delay_loop_features.size();i++)
        {
            for(int j=0;j<state->delay_loop_features[i].size();j++)
            {
                loop_features.push_back(state->delay_loop_features[i][j]);

            }
        }
        bool flag=false;
        state->iter_count=0;
        if(state->_options.use_schmidt)
            flag=updaterMSCKF->update_skf_with_KF3(state,loop_features,false);
        else
            flag=updaterMSCKF->update_noskf_with_KF(state,loop_features,false);
        
        //for recompute transform
        if(flag==false&&state->iter&&state->_options.iter_num==0)
        {
            relinear_t1=boost::posix_time::microsec_clock::local_time();
            cout<<"in recompute"<<endl;
            // sleep(1);
            Vector3d p_loopIncur;
            Matrix3d R_loopTocur;
            if(index_kf!=-1)
            {
              Keyframe* loop_kf=loop_kfs[index_kf];
              vector<Keyframe*> kf_list;
              kf_list.push_back(loop_kf);
              int id=0;
              assert(loop_kf!=nullptr);
              if(loop_kf->PnPRANSAC(uv_norm,uv_3d_loop,p_loopIncur,R_loopTocur,params.match_num_thred))
              {

                  ComputeRelativePose(cur_kf_0,loop_kf,p_loopIncur,R_loopTocur);
                  count_1++;

              }
            }
            
            
            duration=state->_timestamp-last_kf_match_time;
            // state->iter_count=state->_options.iter_num-1;          
            if(state->_options.use_schmidt)
                flag=updaterMSCKF->update_skf_with_KF3(state,loop_features,false);
            else
                flag=updaterMSCKF->update_noskf_with_KF(state,loop_features,false);
            state->iter=false;
            reset_map_count++;
            relinear_t2=boost::posix_time::microsec_clock::local_time();
            relinear_time+=(relinear_t2-relinear_t1).total_microseconds() * 1e-6;
        }
    
        
        if(flag)          
            count_msckf_with_kf++;

         for(Feature* feat: loop_features){
                feat->to_delete = true;
        }

        // auto iter1=state->delay_loop_features.begin();
        // state->delay_loop_features.erase(iter1);
        state->delay_loop_features.clear();
        // auto iter2=state->delay_clones.begin();
        // state->delay_clones.erase(iter2);
        state->delay_clones.clear();
    }


    //===================================================================================
    // Update our visualization feature set, and clean up the old features
    //===================================================================================


    // Collect all slam features into single vector
    std::vector<Feature*> features_used_in_update = featsup_MSCKF;
    // features_used_in_update.insert(features_used_in_update.end(),feats_kf_linked.begin(),feats_kf_linked.end());
    features_used_in_update.insert(features_used_in_update.end(), feats_slam_UPDATE.begin(), feats_slam_UPDATE.end());
    features_used_in_update.insert(features_used_in_update.end(), feats_slam_DELAYED.begin(), feats_slam_DELAYED.end());
    // features_used_in_update.insert(features_used_in_update.end(),feats_with_loop.begin(),feats_with_loop.end());
    // features_used_in_update.insert(features_used_in_update.end(),feats_slam_kf_linked.begin(),feats_slam_kf_linked.end());
    update_keyframe_historical_information(features_used_in_update);
    cout<<"after historical information"<<endl;

    // Save all the MSCKF features used in the update
    good_features_MSCKF.clear();
    for(Feature* feat : featsup_MSCKF) {
        good_features_MSCKF.push_back(feat->p_FinG);
        feat->to_delete = true;
    }


    // Remove features that where used for the update from our extractors at the last timestep
    // This allows for measurements to be used in the future if they failed to be used this time
    // Note we need to do this before we feed a new image, as we want all new measurements to NOT be deleted
    trackFEATS->get_feature_database()->cleanup();
    if(trackARUCO != nullptr) {
        trackARUCO->get_feature_database()->cleanup();
    }

    //===================================================================================
    // Cleanup, marginalize out what we don't need any more...
    //===================================================================================

    // First do anchor change if we are about to lose an anchor pose
    updaterSLAM->change_anchors(state);
    cout<<"after change_anchors"<<endl;

    // Cleanup any features older then the marginalization time
    trackFEATS->get_feature_database()->cleanup_measurements(state->margtimestep());
    if(trackARUCO != nullptr) {
        trackARUCO->get_feature_database()->cleanup_measurements(state->margtimestep());
    }

    // Finally marginalize the oldest clone if needed
    StateHelper::marginalize_old_clone(state);
    cout<<"after marginalize"<<endl;

    // Finally if we are optimizing our intrinsics, update our trackers
    if(state->_options.do_calib_camera_intrinsics) {
        // Get vectors arrays
        std::map<size_t, Eigen::VectorXd> cameranew_calib;
        std::map<size_t, bool> cameranew_fisheye;
        for(int i=0; i<state->_options.num_cameras; i++) {
            Vec* calib = state->_cam_intrinsics.at(i);
            bool isfish = state->_cam_intrinsics_model.at(i);
            cameranew_calib.insert({i,calib->value()});
            cameranew_fisheye.insert({i,isfish});
        }
        // Update the trackers and their databases
        trackFEATS->set_calibration(cameranew_calib, cameranew_fisheye, true);
        if(trackARUCO != nullptr) {
            trackARUCO->set_calibration(cameranew_calib, cameranew_fisheye, true);
        }
    }
    rT7 =  boost::posix_time::microsec_clock::local_time();


    //===================================================================================
    // Debug info, and stats tracking
    //===================================================================================

    // Get timing statitics information
    double time_track = (rT2-rT1).total_microseconds() * 1e-6;
    double time_prop = (rT3-rT2).total_microseconds() * 1e-6;
    double time_msckf = (rT4-rT3).total_microseconds() * 1e-6;
    double time_slam_update = (rT5-rT4).total_microseconds() * 1e-6;
    double time_slam_delay = (rT6-rT5).total_microseconds() * 1e-6;
    double time_marg = (rT7-rT6).total_microseconds() * 1e-6;
    double time_total = (rT7-rT1).total_microseconds() * 1e-6;

    // Timing information
    printf(BLUE "[TIME]: %.4f seconds for tracking\n" RESET, time_track);
    printf(BLUE "[TIME]: %.4f seconds for propagation\n" RESET, time_prop);
    printf(BLUE "[TIME]: %.4f seconds for MSCKF update (%d features)\n" RESET, time_msckf, (int)featsup_MSCKF.size());
    if(state->_options.max_slam_features > 0) {
        printf(BLUE "[TIME]: %.4f seconds for SLAM update (%d feats)\n" RESET, time_slam_update, (int)feats_slam_UPDATE.size());
        printf(BLUE "[TIME]: %.4f seconds for SLAM delayed init (%d feats)\n" RESET, time_slam_delay, (int)feats_slam_DELAYED.size());
    }
    printf(BLUE "[TIME]: %.4f seconds for marginalization (%d clones in state)\n" RESET, time_marg, (int)state->_clones_IMU.size());
    printf(BLUE "[TIME]: %.4f seconds for total\n" RESET, time_total);
    

    // Finally if we are saving stats to file, lets save it to file
    if(params.record_timing_information && of_statistics.is_open()) {
        // We want to publish in the IMU clock frame
        // The timestamp in the state will be the last camera time
        double t_ItoC = state->_calib_dt_CAMtoIMU->value()(0);
        double timestamp_inI = state->_timestamp + t_ItoC;
        // Append to the file
        of_statistics << std::fixed << std::setprecision(15)
                      << timestamp_inI << ","
                      << std::fixed << std::setprecision(5)
                      << time_track << "," << time_prop << "," << time_msckf << ",";
        if(state->_options.max_slam_features > 0) {
            of_statistics << time_slam_update << "," << time_slam_delay << ",";
        }
        of_statistics << time_marg << "," << time_total << std::endl;
        of_statistics.flush();
    }


    // Update our distance traveled
    if(timelastupdate != -1 && state->_clones_IMU.find(timelastupdate) != state->_clones_IMU.end()) {
        Eigen::Matrix<double,3,1> dx = state->_imu->pos() - state->_clones_IMU.at(timelastupdate)->pos();
        distance += dx.norm();
    }
    timelastupdate = timestamp;

    // Debug, print our current state
    printf("q_GtoI = %.3f,%.3f,%.3f,%.3f | p_IinG = %.3f,%.3f,%.3f | dist = %.2f (meters)\n",
            state->_imu->quat()(0),state->_imu->quat()(1),state->_imu->quat()(2),state->_imu->quat()(3),
            state->_imu->pos()(0),state->_imu->pos()(1),state->_imu->pos()(2),distance);
    printf("bg = %.4f,%.4f,%.4f | ba = %.4f,%.4f,%.4f\n",
             state->_imu->bias_g()(0),state->_imu->bias_g()(1),state->_imu->bias_g()(2),
             state->_imu->bias_a()(0),state->_imu->bias_a()(1),state->_imu->bias_a()(2));
    printf("map is initialized : %d\n",map_initialized); 
    printf("msckf_with_kf: %d |slam_with_kf: %d |slam_delay_with_kf: %d\n",count_msckf_with_kf,count_slam_with_kf,count_slam_delay_with_kf);               
    printf("nuisance size: %d\n",state->_clones_Keyframe.size());
    printf("last kf match time: %.4f ;current time: %.4f; kf_interval: %.4f\n",last_kf_match_time,timestamp,params.kf_match_interval);
    printf("reset_tranform count: %d\n",reset_map_count);
    printf("avg relinear time: %f \n",relinear_time/reset_map_count);
    printf("dt from last kf_match: %.4f\n",duration);
    printf("optimize_count: %d | optimize_time: %.4f\n",optimize_count,optimize_time);
    printf("num feature id: %d\n", trackFEATS->get_currid());
    printf("count_1: %d count_2: %d count_3: %d count_4 %d\n", count_1,count_2,count_3,count_4);

    printf("map_number: %d\n", params.num_map);
    for(int i=0;i<params.num_map;i++)
    {
      printf("-----map %d is initialized: %d ------\n", i, state->set_transforms[i]);
    }
    

    if(map_initialized)
    {
         printf("transform local: %d\n",int(state->transform_vio_to_map->id()));
        // MatrixXd cov_trans=state->_Cov.block(state->transform_vio_to_map->id(),state->transform_vio_to_map->id(),6,6);
        // cout<<cov_trans<<endl;
        cout<<"transform: "<<state->transform_vio_to_map->pos().transpose()<<endl;
        Eigen::Vector3d pos=state->transform_vio_to_map->pos();
        Eigen::Vector4d rot=state->transform_vio_to_map->quat();
        of_transform.precision(5);
        of_transform.setf(std::ios::fixed, std::ios::floatfield);
        of_transform<<timestamp<<" ";
        of_transform.precision(6);
        of_transform<<pos(0)<<" "<<pos(1)<<" "<<pos(2)<<" "<<rot(0)<<" "<<rot(1)<<" "<<rot(2)<<" "<<rot(3)<<" ";
        // of_transform.precision(10);
        // of_transform<<cov_trans(0,0)<<" "<<cov_trans(0,1)<<cov_trans(0,2)<<" "<<cov_trans(1,1)<<" "<<cov_trans(1,2)<<" "<<cov_trans(2,2)<<" "
        //             <<cov_trans(3,3)<<" "<<cov_trans(3,4)<<" "<<cov_trans(3,5)<<" "<<cov_trans(4,4)<<" "<<cov_trans(4,5)<<" "<<cov_trans(5,5)<<endl;
    }
        
    // Debug for camera imu offset
    if(state->_options.do_calib_camera_timeoffset) {
        printf("camera-imu timeoffset = %.5f\n",state->_calib_dt_CAMtoIMU->value()(0));
    }


    // Debug for camera intrinsics
    if(state->_options.do_calib_camera_intrinsics) {
        for(int i=0; i<state->_options.num_cameras; i++) {
            Vec* calib = state->_cam_intrinsics.at(i);
            printf("cam%d intrinsics = %.3f,%.3f,%.3f,%.3f | %.3f,%.3f,%.3f,%.3f\n",(int)i,
                     calib->value()(0),calib->value()(1),calib->value()(2),calib->value()(3),
                     calib->value()(4),calib->value()(5),calib->value()(6),calib->value()(7));
        }
    }

    // Debug for camera extrinsics
    if(state->_options.do_calib_camera_pose) {
        for(int i=0; i<state->_options.num_cameras; i++) {
            PoseJPL* calib = state->_calib_IMUtoCAM.at(i);
            printf("cam%d extrinsics = %.3f,%.3f,%.3f,%.3f | %.3f,%.3f,%.3f\n",(int)i,
                     calib->quat()(0),calib->quat()(1),calib->quat()(2),calib->quat()(3),
                     calib->pos()(0),calib->pos()(1),calib->pos()(2));
        }
    }
    cout<<"finish print information"<<endl;


}


void VioManager::update_keyframe_historical_information(const std::vector<Feature*> &features) {


    // Loop through all features that have been used in the last update
    // We want to record their historical measurements and estimates for later use
    for(const auto &feat : features) {

        // Get position of feature in the global frame of reference
        Eigen::Vector3d p_FinG = feat->p_FinG;

        // If it is a slam feature, then get its best guess from the state
        if(state->_features_SLAM.find(feat->featid)!=state->_features_SLAM.end()) {
            p_FinG = state->_features_SLAM.at(feat->featid)->get_xyz(false);
        }

        // Push back any new measurements if we have them
        // Ensure that if the feature is already added, then just append the new measurements
        if(hist_feat_posinG.find(feat->featid)!=hist_feat_posinG.end()) {
            hist_feat_posinG.at(feat->featid) = p_FinG;
            for(const auto &cam2uv : feat->uvs) {
                if(hist_feat_uvs.at(feat->featid).find(cam2uv.first)!=hist_feat_uvs.at(feat->featid).end()) {
                    hist_feat_uvs.at(feat->featid).at(cam2uv.first).insert(hist_feat_uvs.at(feat->featid).at(cam2uv.first).end(), cam2uv.second.begin(), cam2uv.second.end());
                    hist_feat_uvs_norm.at(feat->featid).at(cam2uv.first).insert(hist_feat_uvs_norm.at(feat->featid).at(cam2uv.first).end(), feat->uvs_norm.at(cam2uv.first).begin(), feat->uvs_norm.at(cam2uv.first).end());
                    hist_feat_timestamps.at(feat->featid).at(cam2uv.first).insert(hist_feat_timestamps.at(feat->featid).at(cam2uv.first).end(), feat->timestamps.at(cam2uv.first).begin(), feat->timestamps.at(cam2uv.first).end());
                } else {
                    hist_feat_uvs.at(feat->featid).insert(cam2uv);
                    hist_feat_uvs_norm.at(feat->featid).insert({cam2uv.first,feat->uvs_norm.at(cam2uv.first)});
                    hist_feat_timestamps.at(feat->featid).insert({cam2uv.first,feat->timestamps.at(cam2uv.first)});
                }
            }
        } else {
            hist_feat_posinG.insert({feat->featid,p_FinG});
            hist_feat_uvs.insert({feat->featid,feat->uvs});
            hist_feat_uvs_norm.insert({feat->featid,feat->uvs_norm});
            hist_feat_timestamps.insert({feat->featid,feat->timestamps});
        }
    }


    // Go through all our old historical vectors and find if any features should be removed
    // In this case we know that if we have no use for features that only have info older then the last marg time
    std::vector<size_t> ids_to_remove;
    for(const auto &id2feat : hist_feat_timestamps) {
        bool all_older = true;
        for(const auto &cam2time : id2feat.second) {
            for(const auto &time : cam2time.second) {
                if(time >= hist_last_marginalized_time) {
                    all_older = false;
                    break;
                }
            }
            if(!all_older) break;
        }
        if(all_older) {
            ids_to_remove.push_back(id2feat.first);
        }
    }

    // Remove those features!
    for(const auto &id : ids_to_remove) {
        hist_feat_posinG.erase(id);
        hist_feat_uvs.erase(id);
        hist_feat_uvs_norm.erase(id);
        hist_feat_timestamps.erase(id);
    }

    // Remove any historical states older then the marg time
    auto it0 = hist_stateinG.begin();
    while(it0 != hist_stateinG.end()) {
        if(it0->first < hist_last_marginalized_time) it0 = hist_stateinG.erase(it0);
        else it0++;
    }

    // If we have reached our max window size record the oldest clone
    // This clone is expected to be marginalized from the state
    if ((int) state->_clones_IMU.size() > state->_options.max_clone_size) {
        hist_last_marginalized_time = state->margtimestep();
        assert(hist_last_marginalized_time != INFINITY);
        Eigen::Matrix<double,7,1> imustate_inG = Eigen::Matrix<double,7,1>::Zero();
        imustate_inG.block(0,0,4,1) = state->_clones_IMU.at(hist_last_marginalized_time)->quat();
        imustate_inG.block(4,0,3,1) = state->_clones_IMU.at(hist_last_marginalized_time)->pos();
        hist_stateinG.insert({hist_last_marginalized_time, imustate_inG});
    }

}




void VioManager::loadPoseGraph_multimap(std::string map_save_path, std::vector<std::string> pose_graph_filenames, std::vector<std::string> keyframe_pose_filenames)
{
    boost::posix_time::ptime t1,t2;
    t1 = boost::posix_time::microsec_clock::local_time();
    max_kf_id=-1;
    int count=0;
    int add_kf_num=0;
    int nums=0;

    for(int map_seq=0; map_seq<pose_graph_filenames.size(); map_seq++)
    {
      ifstream f1,f2;
      string file_match = map_save_path + pose_graph_filenames[map_seq];
      string file_pose = map_save_path + keyframe_pose_filenames[map_seq];

      state->set_transforms.insert({map_seq,false});

      printf("load matches from: %s \n", file_match.c_str());
      printf("load keyframe pose from: %s \n", file_pose.c_str());
      printf("loading...\n");

      f1.open(file_match.data());
      assert(f1.is_open());
      
      
      string str,result;
      int line_num=0;
      int match_num=0;
      double query_ts,kf_ts;
      
      VectorXd intrinsics=params.camera_intrinsics[0];
      int is_fisheye=params.camera_fisheye[0];
      string image_name1,image_name2;
      int match_num_per_img=0;
      int total_line_num_per_img=0;
      bool skip=false;
      while (getline(f1,str))
      {
          if(line_num%2==0)
          {
            line_num++;
            stringstream line(str);
            line>>result;
        //    cout<<"***"<<result<<endl;
            image_name1=result;

            line>>result;
        //    cout<<"==="<<result<<endl;
            image_name2=result;

            line>>result;
            match_num=stoi(result);

            if(match_num<params.match_num_thred)
            {
              skip=true;
              continue;
            }

            /******for euroc**********/
            string image_name_front;
            string image_name_back;
            image_name_front=image_name1.substr(0,10);
            image_name_front=image_name_front+".";
            image_name_back=image_name1.substr(10,1); 
            string image_name_final1=image_name_front+image_name_back;

            query_ts=stod(image_name_final1);
            if(std::find(query_timestamps.begin(),query_timestamps.end(),query_ts)==query_timestamps.end())
              query_timestamps.push_back(query_ts);
        
        
            image_name_front=image_name2.substr(0,10);
            image_name_front=image_name_front+".";
            image_name_back=image_name2.substr(10,1);
            string image_name_final2=image_name_front+image_name_back;
            cout<<"image_name_final2: "<<image_name_final2<<endl;
            kf_ts=stod(image_name_final2);


          }
          else if(line_num%2==1)
          {
            line_num++;
            if(skip) //skip the line where the number of matches is smaller than the thred
            {
              skip=false;
              continue;
            }

            Keyframe* kf=nullptr;
            kf=state->get_kfdataBase()->get_keyframe(kf_ts);
            bool new_kf=false;
            if(kf==nullptr)
            {
                cout<<"this is the new keyframe"<<endl;
                kf=new Keyframe();
                max_kf_id++;
                kf->index=max_kf_id;
                kf->cam_id=0;  //kf is from left cam
                intrinsics.conservativeResize(9,1);
                intrinsics(8)=is_fisheye;  //is fisheye or not
                kf->_intrinsics=intrinsics;
                kf->sequence=map_seq;
                new_kf=true;
            }
            else if(kf->sequence!=map_seq) //There might be a situation that the matched kfs have the same timestamp but come from different sequence. For simplicity, we igonre the current sequence's matched kf.
            {
              continue;
            }
            stringstream line(str);
            cout<<"match num:"<<match_num<<endl;
            vector<cv::Point2f> match_points,match_points_norm,kf_points,kf_points_norm;
            vector<cv::Point3f> kf3d_points;
            
            for(int i=0;i<match_num;i++)
            {
                cv::Point2f q2d,kf2d,projectkf2d;
                cv::Point3f kf3d;
                line>>result;
                //    cout<<"uv_x: "<<result<<" ";
                q2d.x=stof(result);
                //    cout<<"uv_x load: "<<q2d.x<<endl;
                line>>result;
                //    cout<<"uv_y: "<<result<<" ";
                q2d.y=stof(result);
                //    cout<<"uv_y load: "<<q2d.y<<endl;
                line>>result;
                //    cout<<"kuv_x: "<<result<<" ";
                kf2d.x=stof(result);
                //    cout<<"kuv_x load: "<<kf2d.x<<endl;
                line>>result;
                //    cout<<"kuv_y: "<<result<<" ";
                kf2d.y=stof(result);
                //    cout<<"kuv_y load: "<<kf2d.y<<endl;
                line>>result;
                //    cout<<"k3d_x: "<<result<<" ";
                kf3d.x=stof(result);
                //    cout<<"k3d_x load: "<<kf3d.x<<endl;
                line>>result;
                //    cout<<"k3d_y: "<<result<<" ";
                kf3d.y=stof(result);
                //    cout<<"k3d_y load: "<<kf3d.y<<endl;
                line>>result;
                //    cout<<"k3d_z: "<<result<<" ";
                kf3d.z=stof(result);
                //    cout<<"k3d_z load: "<<kf3d.z<<endl;
                Eigen::Vector3d pt_c(0,0,1);
                pt_c(0)=kf3d.x/kf3d.z;
                pt_c(1)=kf3d.y/kf3d.z;
                double fx=intrinsics(0);
                double fy=intrinsics(1);
                double cx=intrinsics(2);
                double cy=intrinsics(3);
                double k1=intrinsics(4);
                double k2=intrinsics(5);
                double k3=intrinsics(6);
                double k4=intrinsics(7);   

                cv::Point2f q2d_norm = trackFEATS->undistort_point(q2d, 0);
                cv::Point2f kf2d_norm = trackFEATS->undistort_point(kf2d, 0);
                
                
                
                bool q2d_is_exist=false;
                bool kf2d_is_exist=false;
                for(int k=0;k<match_points.size();k++)
                {
                    if(match_points[k]==q2d)
                    {
                        q2d_is_exist=true;
                        break;
                    }
                }
                for(int k=0;k<kf_points.size();k++)
                {
                    if(kf_points[k]==kf2d)
                    {
                        kf2d_is_exist=true;
                    }
                }
                if(q2d_is_exist==false&&kf2d_is_exist==false)
                {
                    match_points.push_back(q2d);
                    match_points_norm.push_back(q2d_norm);
                    kf_points.push_back(kf2d);
                    kf_points_norm.push_back(kf2d_norm);
                    kf3d_points.push_back(kf3d);
                    
                }
                    
                
            }
            if(match_points.size()>=params.match_num_thred)
            {
                    nums++;
                   
            }
            kf->matched_point_2d_uv_map.insert({query_ts,match_points});
            kf->matched_point_2d_norm_map.insert({query_ts,match_points_norm});
            kf->point_2d_uv_map.insert({query_ts,kf_points});
            kf->point_2d_uv_norm_map.insert({query_ts,kf_points_norm});
            kf->point_3d_map.insert({query_ts,kf3d_points});
            kf->point_3d_linp_map.insert({query_ts,kf3d_points});


            kf->time_stamp=kf_ts;
            kf->loop_img_timestamp_vec.push_back(query_ts);
            kf->image_name=image_name2;

            if(matching_info.find(query_ts)==matching_info.end())
            {
              vector<double> tmp_kf_ts;
              tmp_kf_ts.push_back(kf_ts);
              matching_info.insert({query_ts,tmp_kf_ts});
            }
            else
            {
              matching_info[query_ts].push_back(kf_ts);
            }
            

            

            f2.open(file_pose.data());
            assert(f2.is_open());
            string str1,res1;
            bool get_pose=false;
            if(!new_kf)
            {
              get_pose=true;
            }
            else
            {
              while(getline(f2,str1))  //timestamp,tx,ty,tz,qx,qy,qz,qw
              {
                  stringstream line1(str1);
                  line1>>res1;
                  double ts=stod(res1);
                  double timestp=ts;
                  ts=floor(ts*10)/10.0; 
                  
                  if(ts==kf->time_stamp)
                  {
                      line1>>res1;
                      //  cout<<"tx read: "<<res1<<" ";
                      float tx=atof(res1.c_str());
                      //  cout<<"tx load: "<<tx<<endl;
                      line1>>res1;
                      //  cout<<"ty read: "<<res1<<" ";
                      float ty=atof(res1.c_str());
                      //  cout<<"ty load: "<<ty<<endl;
                      line1>>res1;
                      //  cout<<"tz read: "<<res1<<" ";
                      float tz=atof(res1.c_str());
                      //  cout<<"tz load: "<<tz<<endl;
                      line1>>res1;
                      //  cout<<"qx read: "<<res1<<" ";
                      float qx=atof(res1.c_str());
                      //  cout<<"qx load: "<< qx <<endl;
                      line1>>res1;
                      //  cout<<"qy read: "<<res1<<" ";
                      float qy=atof(res1.c_str());
                      //  cout<<"qy load: "<<qy<<endl;
                      line1>>res1;
                      //  cout<<"qz read: "<<res1<<" ";
                      float qz=atof(res1.c_str());
                      //  cout<<"qz load: "<<qz<<endl;
                      line1>>res1;
                      //  cout<<"qw read: "<<res1<<" ";
                      float qw=atof(res1.c_str());

                      line1>>res1;
                      double Pr11=stod(res1);
                      line1>>res1;
                      double Pr12=stod(res1);
                      line1>>res1;
                      double Pr13=stod(res1);
                      line1>>res1;
                      double Pr22=stod(res1);
                      line1>>res1;
                      double Pr23=stod(res1);
                      line1>>res1;
                      double Pr33=stod(res1);

                      line1>>res1;
                      double Pt11=stod(res1);
                      line1>>res1;
                      double Pt12=stod(res1);
                      line1>>res1;
                      double Pt13=stod(res1);
                      line1>>res1;
                      double Pt22=stod(res1);
                      line1>>res1;
                      double Pt23=stod(res1);
                      line1>>res1;
                      double Pt33=stod(res1);

                      kf->P=Matrix<double,6,6>::Identity();
                      Matrix3d Pr=Matrix3d::Identity();
                      Pr<<Pr11,Pr12,Pr13,
                          Pr12,Pr22,Pr23,
                          Pr13,Pr23,Pr33;
                      Matrix3d Pt=Matrix3d::Identity();
                      Pt<<Pt11,Pt12,Pt13,
                          Pt12,Pt22,Pt23,
                          Pt13,Pt23,Pt33;
                      kf->P.block(0,0,3,3)=Pr;
                      kf->P.block(3,3,3,3)=Pt;

                     
                      //here we load the quaternion in the form of Hamilton,we need to tranform it into JPL
                      Quaterniond q1(qw,qx,qy,qz);  
                      Quaterniond q1_JPL(q1.toRotationMatrix().transpose());
                      Eigen::Matrix<double,7,1> q_kfInw;
                      q_kfInw<<q1_JPL.x(),q1_JPL.y(),q1_JPL.z(),q1_JPL.w(),tx, ty, tz;
                      kf->_Pose_KFinWorld->set_value(q_kfInw);
                      cout<<"get keyframe pose"<<endl;
                      get_pose=true;
                      count++;
                      
                      break;
                  }

              }
            }
            f2.close(); 

            //add curkf into keyframe database
            if(new_kf&&get_pose&&kf->matched_point_2d_uv_map.at(query_ts).size()>0)
            {
                state->insert_keyframe_into_database(kf);
                add_kf_num++;
            }
            else if(new_kf&&!get_pose)
            {
              max_kf_id--;
            }
          }
          
      }    
      f1.close();
      

      if(params.load_map_traj)
      {
        f2.open(file_pose.data());
        assert(f2.is_open());
        string str1,res1;
        vector<PoseJPL*> poses;
        poses.clear();
           
        while(getline(f2,str1))  //timestamp,tx,ty,tz,qx,qy,qz,qw
        {
          stringstream line1(str1);
          line1>>res1;
            
            
          line1>>res1;
          //  cout<<"tx read: "<<res1<<" ";
          float tx=atof(res1.c_str());
          //  cout<<"tx load: "<<tx<<endl;
          line1>>res1;
          //  cout<<"ty read: "<<res1<<" ";
          float ty=atof(res1.c_str());
          //  cout<<"ty load: "<<ty<<endl;
          line1>>res1;
          //  cout<<"tz read: "<<res1<<" ";
          float tz=atof(res1.c_str());
          //  cout<<"tz load: "<<tz<<endl;
          line1>>res1;
          //  cout<<"qx read: "<<res1<<" ";
          float qx=atof(res1.c_str());
          //  cout<<"qx load: "<< qx <<endl;
          line1>>res1;
          //  cout<<"qy read: "<<res1<<" ";
          float qy=atof(res1.c_str());
          //  cout<<"qy load: "<<qy<<endl;
          line1>>res1;
          //  cout<<"qz read: "<<res1<<" ";
          float qz=atof(res1.c_str());
          //  cout<<"qz load: "<<qz<<endl;
          line1>>res1;
          //  cout<<"qw read: "<<res1<<" ";
          float qw=atof(res1.c_str());
          //  cout<<"qw load: "<<qw<<endl;
          //here we load the quaternion in the form of Hamilton,we need to tranform it into JPL
          Quaterniond q1(qw,qx,qy,qz);  
          Quaterniond q1_JPL(q1.toRotationMatrix().transpose());
          Eigen::Matrix<double,7,1> q_kfInw;
          PoseJPL* pose=new PoseJPL();
          q_kfInw<<q1_JPL.x(),q1_JPL.y(),q1_JPL.z(),q1_JPL.w(),tx, ty, tz;
          pose->set_value(q_kfInw);
          poses.push_back(pose);

        }
        map_poses.insert({map_seq,poses});
        cout<<"seq vs poses size: "<<map_seq<<" "<<poses.size()<<endl;
        f2.close();
      }

    }


    
        

    cout<<"count: "<<count<<" add_kf_num: "<<add_kf_num<<endl;
    cout<<"kfdb size: "<<state->get_kfdataBase()->get_internal_data().size()<<endl;
    assert(add_kf_num==state->get_kfdataBase()->get_internal_data().size());
    cout<<"match num > macth num thred: "<<nums<<endl;
    max_kf_id_in_database=max_kf_id;


    //random select destination
    std::mt19937 gen_destination;
    int seed =1;
    gen_destination = std::mt19937(1);
    gen_destination.seed(1);
    std::uniform_int_distribution<int> gen_id(add_kf_num/2-1,add_kf_num-1);
    int destination_id = gen_id(gen_destination);
    cout<<"desitnation_id: "<<destination_id<<endl;
    state->destination_kf= state->get_kfdataBase()->get_keyframe(destination_id);
    assert(state->destination_kf !=nullptr);
    int seq_dest= state->destination_kf->sequence;
    string pose_graph_name = pose_graph_filenames[seq_dest];
    string scene = pose_graph_name.substr(7,2);
    string seq_number = pose_graph_name.substr(9,2);
    cout<<"kf_dest: "<<to_string(state->destination_kf->time_stamp)<<" kf_dest seq: "<<seq_dest<<endl;
    

    t2=boost::posix_time::microsec_clock::local_time();
    printf("load pose graph time: %f s\n", (t2-t1).total_microseconds() * 1e-6);
    sleep(2);

}



bool VioManager::ComputeRelativePose(Keyframe *cur_kf, Keyframe *loop_kf,Vector3d &p_loopIncur, Matrix3d &R_loopTocur) {
    //get the current image pose in current vio system reference

    
    Matrix3d R_I_to_G =state->_imu->Rot().transpose();
    Matrix3d R_C_to_I =state->_calib_IMUtoCAM[cur_kf->cam_id]->Rot().transpose();
    Matrix3d R_kf_to_G= R_I_to_G*R_C_to_I ; //R_GI*R_Ic=R_Gc;
    Vector3d p_I_in_G=state->_imu->pos();
    Vector3d p_I_in_C= state->_calib_IMUtoCAM[cur_kf->cam_id]->pos();
    Vector3d p_kf_in_G=p_I_in_G-R_kf_to_G*p_I_in_C; //p_cinG=p_iinG+p_icinG=p_iinG-p_ciinG=p_iinG-R_ctoG*p_ciinc;



    //set the current image pose in keyframe class
    cur_kf->vio_R_w_i=R_kf_to_G;
    cur_kf->vio_T_w_i=p_kf_in_G;
    Matrix<double,4,1> q_kf_to_G=rot_2_quat(R_kf_to_G);
    Matrix<double,7,1> pos;
    pos<<q_kf_to_G(0,0),q_kf_to_G(1,0),q_kf_to_G(2,0),q_kf_to_G(3,0),p_kf_in_G(0,0),p_kf_in_G(1,0),p_kf_in_G(2,0);
    PoseJPL *p=new PoseJPL();
    p->set_value(pos);
    cur_kf->_Pose_KFinVIO=dynamic_cast<PoseJPL*>(p->clone());
    cout<<"after set pos"<<endl;




    //get the current frame pose in current vio system reference.
    Matrix3d cur_vio_R_cur_kf=cur_kf->vio_R_w_i;
    Vector3d cur_vio_p_cur_kf=cur_kf->vio_T_w_i;
    //get the loop_kf pose in the map reference.
    Matrix3d map_ref_R_loop_kf;
    Vector3d map_ref_p_loop_kf;
    map_ref_R_loop_kf=loop_kf->_Pose_KFinWorld->Rot();
    map_ref_p_loop_kf=loop_kf->_Pose_KFinWorld->pos();
    //get the pose from cur_frame to loop_frame
    Vector3d relative_t; //cur_frame in loop_frame
    Matrix3d relative_q; //cur_frame to loop_frame
    relative_t=-R_loopTocur.transpose()*p_loopIncur;
    relative_q=R_loopTocur.transpose();
    //compute the pose from vio system ref to map ref (vio in map)
    Matrix3d map_ref_R_cur_vio=map_ref_R_loop_kf*relative_q*cur_vio_R_cur_kf.transpose(); //R_wkf*R_kfcur*R_viocur.transpose()
    Vector3d cur_kf_p_cur_vio=-cur_vio_R_cur_kf.transpose()*cur_vio_p_cur_kf;
    Vector3d cur_loop_p_cur_vio=relative_t+relative_q*cur_kf_p_cur_vio;

    Vector3d map_ref_p_cur_vio=map_ref_p_loop_kf+map_ref_R_loop_kf*cur_loop_p_cur_vio;



    Matrix<double,4,1> q_vio_to_map=rot_2_quat(map_ref_R_cur_vio);
    Matrix<double,7,1> pose_vio_to_map;
    pose_vio_to_map<<q_vio_to_map,map_ref_p_cur_vio;
    // Matrix<double,7,1> pose_vio_to_map;
    // pose_vio_to_map<<0,0,0,1,0.01,0.01,0.01;
    if(!params.use_multi_map)
    {
      if(map_initialized==false)  //initialized the relative pose between vins and map
      {
        if(StateHelper::add_map_transformation(state,pose_vio_to_map))
        {
            map_initialized=true;
            state->set_transform=true;
            return true;
        }

        
      }
      else if(map_initialized==true)  //it has been a long-time from last_kf_match_time, we recompute the relative pose between vins and map
      {

       state->transform_vio_to_map->set_linp(pose_vio_to_map);
       return true;
      }
    }
    else //use_multi map
    {
      int seq_id = loop_kf->sequence;
      if(state->set_transforms[seq_id])
      {
        state->transform_vio_to_maps[seq_id]->set_linp(pose_vio_to_map);
        return true;
      }
      else
      {
        if(StateHelper::add_map_transformation(state,pose_vio_to_map,seq_id))
        {
            map_initialized=true;
            state->set_transforms[seq_id]=true;
            return true;
        }
      }
    }
    
    
    
    return false;

}



vector<Keyframe*> VioManager::get_loop_kfs(State *state)
{
    
    vector<Keyframe*> res;
    double ts= state->_timestamp;
    std::cout<<"***"<<to_string(ts)<<"***"<<endl;
    // ts=floor(ts*100);
    // cout<<"ts*100: "<<to_string(ts)<<endl;
    // ts=ts/100.0+0.01;
    // cout<<"ts/100.0: "<<to_string(ts)<<endl;
    ts=floor(ts*10)/10.0;  //保留3位小数 for euroc ;保留2位小数for kaist and eight circle
    
    std::cout<<"in get_loop_kf"<<endl;
    std::cout.precision(14);
    std::cout<<"***"<<ts<<"***"<<endl;
    KeyframeDatabase* db=state->get_kfdataBase();
    //ts for YQ and simulation (not need for eight)
    // ts=get_approx_ts(ts);
    // if(ts<0)
    // {
    //     return res;
    // }
    // end ts for YQ and simulation
    state->_timestamp_approx=ts;
    std::cout.precision(14);
    std::cout<<"***"<<ts<<"***"<<endl;


    vector<double> kfs_ts;
    if(params.use_multi_map)
    {
      kfs_ts=matching_info[ts];

      for(int i=0;i<kfs_ts.size();i++)
      {
          Keyframe* kf=db->get_keyframe(kfs_ts[i]);
          res.push_back(kf);
      }
    }
    else
    {
      kfs_ts=db->get_match_kfs(ts);

      if(kfs_ts.empty())
      {
        return res;
      }
        
      
      bool have_enough_match=false;
      for(int i=0;i<kfs_ts.size();i++)
      {
          Keyframe* kf=db->get_keyframe(kfs_ts[i]);
          if(kf->point_3d_map.at(ts).size()>=params.match_num_thred)
          {
              cout<<"kfpoints size:"<<kf->point_3d_map.at(ts).size()<<" thred:"<<params.match_num_thred<<endl;
              have_enough_match=true;
              break;
          }
          
      }
      if(have_enough_match)
      {
          for(int i=0;i<kfs_ts.size();i++)
          {
              Keyframe* kf=db->get_keyframe(kfs_ts[i]);
              if(kf->point_3d_map.at(ts).size()>=params.match_num_thred&&res.size()<5)
                  res.push_back(kf);
          }
        
      }
    }
     cout<<"match kf size: "<<res.size()<<endl;

    // sleep(3);
    
    

    return res;
   
    // if(state->_timestamp-last_kf_match_time>params.kf_match_interval)
    // {
    //     cout<<"cur_time: "<<to_string(state->_timestamp)<<" last_kf_match: "<<to_string(last_kf_match_time)<<"==========================================================================="<<endl;
        
    //     scale=0.5;
    // }
 


}



void VioManager::get_multiKF_match_feature(double timestamp, vector<Keyframe*> loop_kfs,int &index,vector<cv::Point2f>& uv_loop,vector<cv::Point3f>& uv_3d_loop, vector<cv::Point2f>& uv, vector<Feature*>& feats_with_loop)
{
    
    // double ts=floor(timestamp*100)/100.0;  //保留4位小数 for euroc; 2 for kaist

    double ts=state->_timestamp_approx;   //for YQ
    int max_match_num=0;
    double match_most_kf;
    //get the loop_kf that has most matches with current frame for transform initial
    for(int i=0;i<loop_kfs.size();i++)
    {
        if(loop_kfs[i]->matched_point_2d_uv_map.at(ts).size()>max_match_num)
        {
            max_match_num=loop_kfs[i]->matched_point_2d_uv_map.at(ts).size();
            // uv=loop_kfs[i]->matched_point_2d_uv_map.at(ts); //the most uv,uv_loop,uv_3d_loop is used to do mapinitialize.
            uv=loop_kfs[i]->matched_point_2d_norm_map.at(ts);
            uv_loop=loop_kfs[i]->point_2d_uv_map.at(ts);
            uv_3d_loop=loop_kfs[i]->point_3d_map.at(ts);
            index=i;
        }
    }
    assert(index>=0);
    assert(uv.size()==uv_loop.size());
    assert(uv_loop.size()==uv_3d_loop.size());


    vector<cv::Point2f> all_uvs_cur;
    int all_kfs_match=0;
    // there might be a situation that a feature in current image is matched with more than one image in loop_kfs
    for(int i=0;i<loop_kfs.size();i++)
    {
        vector<cv::Point2f> match_uvs=loop_kfs[i]->matched_point_2d_uv_map.at(ts);
        // cout<<"for kf: "<<to_string(loop_kfs[i]->time_stamp)<<" with query image: "<<to_string(ts)<<endl;
        all_kfs_match+=match_uvs.size();
        for(int j=0;j<match_uvs.size();j++)
        {
            bool find=false;
            for(int k=0;k<all_uvs_cur.size();k++)
            {
                if(match_uvs[j]==all_uvs_cur[k])
                {
                    find=true;
                    cout<<"find feat: "<<match_uvs[j]<<" in all_uvs_cur: "<<all_uvs_cur[k]<<endl;
                    break;
                }
            }
            if(find==false)
            {
              all_uvs_cur.push_back(match_uvs[j]);
            }
        }
    }
    assert(state->of_points.is_open());
    
    // state->of_points<<"current_time: "<<to_string(state->_timestamp)<<" with "<<loop_kfs.size()<<" kf matches"<<endl<<
    // "all_uvs_cur size: "<<all_uvs_cur.size()<<" VS all_kfs_match"<<all_kfs_match<<endl;

    // for(int i=0;i<loop_kfs.size();i++)
    // {
    //     vector<cv::Point2f> match_uvs=loop_kfs[i]->matched_point_2d_uv_map.at(ts);
    //     size_t cam_id=0;
    //     vector<size_t> id;
    //     trackFEATS->add_features_into_database(timestamp,cam_id,match_uvs,id);
    //     for(int j=0;j<match_uvs.size();j++)
    //     {
    //     Feature* feat=trackFEATS->get_feature_database()->get_feature(id[j]);
    //     cv::Point2f uv_cur=match_uvs[j];
    //     std::unordered_map<double,size_t> match; //loop_kf timestamp and feature local(kf) id
    //     //as this feature is the new extracted feature in current image, it should only be observed once at current time
    //     //  cout<<"feat->timestamps[cam_id].size(): "<<id[i]<<" "<<feat->timestamps[cam_id].size()<<endl;
    //      assert(feat->timestamps[cam_id].size()==1);
    //      assert(feat->timestamps[cam_id][0]==timestamp);
       
    //     match.insert({loop_kfs[i]->time_stamp,j});
    //     feat->keyframe_matched_obs.insert({ts,match});
    //     cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<" feat id: "<<feat->featid<<endl;
    //     feats_with_loop.push_back(feat);
    //     }
    // }


     //add uv feature into featuredatabase as new extracted features
     size_t cam_id=0;
     vector<size_t> id;
     trackFEATS->add_features_into_database(timestamp,cam_id,all_uvs_cur,id);

     //link all_uvs_cur with loop_kf_uvs
     vector<cv::Point2f> uv_feats; 
     for(int i=0;i<all_uvs_cur.size();i++)
     {
        Feature* feat=trackFEATS->get_feature_database()->get_feature(id[i]);
        cv::Point2f uv_cur=all_uvs_cur[i];
        std::unordered_map<double,size_t> match; //loop_kf timestamp and feature local(kf) id
        //as this feature is the new extracted feature in current image, it should only be observed once at current time
        //  cout<<"feat->timestamps[cam_id].size(): "<<id[i]<<" "<<feat->timestamps[cam_id].size()<<endl;
         assert(feat->timestamps[cam_id].size()==1);
         assert(feat->timestamps[cam_id][0]==timestamp);
        for(int j=0;j<loop_kfs.size();j++)
        {
            //for each loop_kf, we find if this loop_kf has match with feat.
            vector<cv::Point2f> match_uvs=loop_kfs[j]->matched_point_2d_uv_map.at(ts);
            for(int k=0;k<match_uvs.size();k++)
            {
                if(match_uvs[k]==uv_cur)//do feature link;
                {
                   match.insert({loop_kfs[j]->time_stamp,k});
                   break;
                }
            }

        }
        feat->keyframe_matched_obs.insert({ts,match});
        // cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<" feat id: "<<feat->featid<<endl;
        feats_with_loop.push_back(feat);
     }
  

    //  //linke uv with uv_loop
    //  vector<cv::Point2f> uv_feats;  //use to debug
    //  for(int i=0;i<uv.size();i++)
    //  {
    //      Feature* feat=trackFEATS->get_feature_database()->get_feature(id[i]);
    //      //as this feature is the new extracted feature in current image, it should only be observed once at current time
    //      assert(feat->timestamps[cam_id].size()==1);
    //      assert(feat->timestamps[cam_id][0]==timestamp);
    //     std::unordered_map<double,size_t> m;  //loop_kf timestamp and feature local(kf) id
    //     m.insert({loop_kf->time_stamp,i});
    //     feat->keyframe_matched_obs.insert({timestamp,m}); //this is the key step to make feature connection
    //     cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<endl;
    //     cv::Point2f f;
    //     f.x=feat->uvs[cam_id][0](0);
    //     f.y=feat->uvs[cam_id][0](1);
    //     uv_feats.push_back(f);  
    //     feats_with_loop.push_back(feat);
    //  }
}



void VioManager::get_multi_map_KF_match_feature(double timestamp, vector<Keyframe*> loop_kfs, int &index,map<int,vector<cv::Point2f>>& uv_loops, map<int,vector<cv::Point3f>>& uv_3d_loops, map<int,vector<cv::Point2f>>& uv_norms, vector<Feature*>& feats_with_loop)
{
    
    // double ts=floor(timestamp*100)/100.0;  //保留4位小数 for euroc; 2 for kaist

    double ts=state->_timestamp_approx;   //for YQ
    int max_match_num=0;
    double match_most_kf;
    //get the loop_kf that has most matches with current frame for transform initial
    vector<Keyframe*> loop_kfs_filtered;
    set<int> seqs;
    for(int i=0;i<loop_kfs.size();i++)
    {
      seqs.insert(loop_kfs[i]->sequence);
    }


    for(int i=0;i<loop_kfs.size();i++)
    {
        int seq_id = loop_kfs[i]->sequence;
        if(state->set_transforms[seq_id]) //if the transform is initilized, we will extract feats_with_loop
        {
          loop_kfs_filtered.push_back(loop_kfs[i]);
          if(seqs.size()==1) //used for relinearization.
          {

            if(loop_kfs[i]->matched_point_2d_uv_map.at(ts).size()>max_match_num)
            {
              vector<cv::Point2f> uv_norm,uv_loop;
              vector<cv::Point3f> uv_3d_loop;
              max_match_num=loop_kfs[i]->matched_point_2d_uv_map.at(ts).size();
              // uv=loop_kfs[i]->matched_point_2d_uv_map.at(ts); //the most uv,uv_loop,uv_3d_loop is used to do mapinitialize.
              uv_norm=loop_kfs[i]->matched_point_2d_norm_map.at(ts);
              uv_loop=loop_kfs[i]->point_2d_uv_map.at(ts);
              uv_3d_loop=loop_kfs[i]->point_3d_map.at(ts);
              index=i;
              assert(uv_norm.size()==uv_loop.size());
              assert(uv_loop.size()==uv_3d_loop.size());
              uv_norms.insert({seq_id,uv_norm});
              uv_loops.insert({seq_id,uv_loop});
              uv_3d_loops.insert({seq_id,uv_3d_loop});
            }
            
          }
        }
        else  //otherwise, we store the uv_norms, uv_3d_loops for initializing the transform.
        {
          vector<cv::Point2f> uv_norm,uv_loop;
          vector<cv::Point3f> uv_3d_loop;
          uv_norm=loop_kfs[i]->matched_point_2d_norm_map.at(ts);
          uv_loop=loop_kfs[i]->point_2d_uv_map.at(ts);
          uv_3d_loop=loop_kfs[i]->point_3d_map.at(ts);
          assert(uv_norm.size()==uv_loop.size());
          assert(uv_loop.size()==uv_3d_loop.size());
          uv_norms.insert({seq_id,uv_norm});
          uv_loops.insert({seq_id,uv_loop});
          uv_3d_loops.insert({seq_id,uv_3d_loop});
        }
    }
    feats_with_loop.clear();
    
    if(loop_kfs_filtered.empty()) //this means all the transforms are not initialized
    {
      return;
    }


    vector<cv::Point2f> all_uvs_cur;
    int all_kfs_match=0;
    // there might be a situation that a feature in current image is matched with more than one image in loop_kfs
    for(int i=0;i<loop_kfs_filtered.size();i++)
    {
        vector<cv::Point2f> match_uvs=loop_kfs_filtered[i]->matched_point_2d_uv_map.at(ts);
        // cout<<"for kf: "<<to_string(loop_kfs[i]->time_stamp)<<" with query image: "<<to_string(ts)<<endl;
        
        all_kfs_match+=match_uvs.size();
        if(i==0)
        {
          all_uvs_cur=match_uvs;
          continue;
        }
        for(int j=0;j<match_uvs.size();j++)
        {
            if(find(all_uvs_cur.begin(),all_uvs_cur.end(),match_uvs[j])!=all_uvs_cur.end())
            {
              cout<<"find feat: "<<match_uvs[j]<<" in all_uvs_cur"<<endl;
            }
            else
            {
              all_uvs_cur.push_back(match_uvs[j]);
            }
        }
    }
    assert(state->of_points.is_open());
    
    // state->of_points<<"current_time: "<<to_string(state->_timestamp)<<" with "<<loop_kfs.size()<<" kf matches"<<endl<<
    // "all_uvs_cur size: "<<all_uvs_cur.size()<<" VS all_kfs_match"<<all_kfs_match<<endl;

    // for(int i=0;i<loop_kfs.size();i++)
    // {
    //     vector<cv::Point2f> match_uvs=loop_kfs[i]->matched_point_2d_uv_map.at(ts);
    //     size_t cam_id=0;
    //     vector<size_t> id;
    //     trackFEATS->add_features_into_database(timestamp,cam_id,match_uvs,id);
    //     for(int j=0;j<match_uvs.size();j++)
    //     {
    //     Feature* feat=trackFEATS->get_feature_database()->get_feature(id[j]);
    //     cv::Point2f uv_cur=match_uvs[j];
    //     std::unordered_map<double,size_t> match; //loop_kf timestamp and feature local(kf) id
    //     //as this feature is the new extracted feature in current image, it should only be observed once at current time
    //     //  cout<<"feat->timestamps[cam_id].size(): "<<id[i]<<" "<<feat->timestamps[cam_id].size()<<endl;
    //      assert(feat->timestamps[cam_id].size()==1);
    //      assert(feat->timestamps[cam_id][0]==timestamp);
       
    //     match.insert({loop_kfs[i]->time_stamp,j});
    //     feat->keyframe_matched_obs.insert({ts,match});
    //     cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<" feat id: "<<feat->featid<<endl;
    //     feats_with_loop.push_back(feat);
    //     }
    // }


     //add uv feature into featuredatabase as new extracted features
     size_t cam_id=0;
     vector<size_t> id;
     trackFEATS->add_features_into_database(timestamp,cam_id,all_uvs_cur,id);

     //link all_uvs_cur with loop_kf_uvs
     vector<cv::Point2f> uv_feats; 
     for(int i=0;i<all_uvs_cur.size();i++)
     {
        Feature* feat=trackFEATS->get_feature_database()->get_feature(id[i]);
        cv::Point2f uv_cur=all_uvs_cur[i];
        std::unordered_map<double,size_t> match; //loop_kf timestamp and feature local(kf) id
        //as this feature is the new extracted feature in current image, it should only be observed once at current time
        //  cout<<"feat->timestamps[cam_id].size(): "<<id[i]<<" "<<feat->timestamps[cam_id].size()<<endl;
         assert(feat->timestamps[cam_id].size()==1);
         assert(feat->timestamps[cam_id][0]==timestamp);
        for(int j=0;j<loop_kfs_filtered.size();j++)
        {
            //for each loop_kf, we find if this loop_kf has match with feat.
            vector<cv::Point2f> match_uvs=loop_kfs_filtered[j]->matched_point_2d_uv_map.at(ts);
            for(int k=0;k<match_uvs.size();k++)
            {
                if(match_uvs[k]==uv_cur)//do feature link;
                {
                   match.insert({loop_kfs_filtered[j]->time_stamp,k});
                   break;
                }
            }

        }
        feat->keyframe_matched_obs.insert({ts,match});
        // cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<" feat id: "<<feat->featid<<endl;
        feats_with_loop.push_back(feat);
     }
  

    //  //linke uv with uv_loop
    //  vector<cv::Point2f> uv_feats;  //use to debug
    //  for(int i=0;i<uv.size();i++)
    //  {
    //      Feature* feat=trackFEATS->get_feature_database()->get_feature(id[i]);
    //      //as this feature is the new extracted feature in current image, it should only be observed once at current time
    //      assert(feat->timestamps[cam_id].size()==1);
    //      assert(feat->timestamps[cam_id][0]==timestamp);
    //     std::unordered_map<double,size_t> m;  //loop_kf timestamp and feature local(kf) id
    //     m.insert({loop_kf->time_stamp,i});
    //     feat->keyframe_matched_obs.insert({timestamp,m}); //this is the key step to make feature connection
    //     cout<<"feat->keyframe_matched_obs size: "<<feat->keyframe_matched_obs.size()<<endl;
    //     cv::Point2f f;
    //     f.x=feat->uvs[cam_id][0](0);
    //     f.y=feat->uvs[cam_id][0](1);
    //     uv_feats.push_back(f);  
    //     feats_with_loop.push_back(feat);
    //  }
}


