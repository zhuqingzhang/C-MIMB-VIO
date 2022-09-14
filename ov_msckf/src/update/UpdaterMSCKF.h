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
#ifndef OV_MSCKF_UPDATER_MSCKF_H
#define OV_MSCKF_UPDATER_MSCKF_H


#include <Eigen/Eigen>
#include "state/State.h"
#include "state/StateHelper.h"
#include "feat/Feature.h"
#include "types/LandmarkRepresentation.h"
#include "feat/FeatureInitializer.h"
#include "feat/FeatureInitializerOptions.h"
#include "utils/quat_ops.h"
#include "utils/colors.h"

#include "UpdaterHelper.h"
#include "UpdaterOptions.h"

#include <boost/math/distributions/chi_squared.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>



namespace ov_msckf {



    /**
     * @brief Will compute the system for our sparse features and update the filter.
     *
     * This class is responsible for computing the entire linear system for all features that are going to be used in an update.
     * This follows the original MSCKF, where we first triangulate features, we then nullspace project the feature Jacobian.
     * After this we compress all the measurements to have an efficient update and update the state.
     */
    class UpdaterMSCKF {

    public:


        /**
         * @brief Default constructor for our MSCKF updater
         *
         * Our updater has a feature initializer which we use to initialize features as needed.
         * Also the options allow for one to tune the different parameters for update.
         *
         * @param options Updater options (include measurement noise value)
         * @param feat_init_options Feature initializer options
         */
        UpdaterMSCKF(UpdaterOptions &options, FeatureInitializerOptions &feat_init_options) : _options(options) {

            // Save our raw pixel noise squared
            _options.sigma_pix_sq = std::pow(_options.sigma_pix,2);

            // Save our feature initializer
            initializer_feat = new FeatureInitializer(feat_init_options);

            // Initialize the chi squared test table with confidence level 0.95
            // https://github.com/KumarRobotics/msckf_vio/blob/050c50defa5a7fd9a04c1eed5687b405f02919b5/src/msckf_vio.cpp#L215-L221
            for (int i = 1; i < 500; i++) {
                boost::math::chi_squared chi_squared_dist(i); //DoF of chi_squared_dist. from 1 to 500
                //https://www.boost.org/doc/libs/1_35_0/libs/math/doc/sf_and_dist/html/math_toolkit/dist/dist_ref/nmp.html#math.dist.cdf
                // it returns a value x (here is chi_squared_table[i]) such that cdf(dist, x) == p(here is 0.95)
                // so that it means 95% of distribution is below x.
                // so if we have a value, which is larger than chi_square_table[i],
                // then, this value are very likely to be an outlier.  by zzq
                chi_squared_table[i] = boost::math::quantile(chi_squared_dist, 0.95);
            }

        }


        /**
         * @brief Given tracked features, this will try to use them to update the state.
         *
         * @param state State of the filter
         * @param feature_vec Features that can be used for update
         */
        void update(State *state, std::vector<Feature*>& feature_vec);

        void update_skf(State *state, std::vector<Feature*>& feature_vec);



        bool update_skf_with_KF3(State *state, std::vector<Feature*>& feature_vec,bool iterative);
       
        bool update_initial_transform(State *state, std::vector<Feature *>& feature_vec);
        
        bool update_noskf_with_KF(State *state, std::vector<Feature *> &feature_vec, bool iterative);


    protected:


        /// Options used during update
        UpdaterOptions _options;

        /// Feature initializer class object
        FeatureInitializer* initializer_feat;

        /// Chi squared 95th percentile table (lookup would be size of residual)
        std::map<int, double> chi_squared_table;

        Eigen::MatrixXd _Hx_big_last;
        Eigen::MatrixXd _Hn_big_last;
        Eigen::VectorXd _res_big_last;
        double _avg_error_2_last;


    };




}




#endif //OV_MSCKF_UPDATER_MSCKF_H


