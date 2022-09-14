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
#include "TrackSIM.h"


using namespace ov_core;




void TrackSIM::feed_measurement_simulation(double timestamp, const std::vector<int> &camids, const std::vector<std::vector<std::pair<size_t,Eigen::VectorXf>>> &feats) {


    // Assert our two vectors are equal
    assert(camids.size()==feats.size());

    // Loop through each camera
    for(size_t i=0; i<camids.size(); i++) {

        // Current camera id
        int cam_id = camids.at(i);

        // Our good ids and points
        std::vector<cv::KeyPoint> good_left;
        std::vector<size_t> good_ids_left;

        // Update our feature database, with theses new observations
        // NOTE: we add the "currid" since we need to offset the simulator
        // NOTE: ids by the number of aruoc tags we have specified as tracking
        for(const auto &feat : feats.at(i)) {

            // Get our id value
            size_t id = feat.first+currid;
            // cout<<"currid: "<<currid<<" "<<"id sim feature: "<<id<<endl;
            // sleep(1); 

            // Create the keypoint
            cv::KeyPoint kpt;
            kpt.pt.x = feat.second(0);
            kpt.pt.y = feat.second(1);
            good_left.push_back(kpt);
            good_ids_left.push_back(id);

            // Append to the database
            cv::Point2f npt_l = undistort_point(kpt.pt, cam_id);
            database->update_feature(id, timestamp, cam_id,
                                     kpt.pt.x, kpt.pt.y, npt_l.x, npt_l.y);
        }

        // Get our width and height
        auto wh = camera_wh.at(cam_id);

        // Move forward in time
        img_last[cam_id] = cv::Mat::zeros(cv::Size(wh.first,wh.second), CV_8UC1);
        pts_last[cam_id] = good_left;
        ids_last[cam_id] = good_ids_left;

    }


}

