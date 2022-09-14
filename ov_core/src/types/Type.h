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
#ifndef OV_TYPE_TYPE_BASE_H
#define OV_TYPE_TYPE_BASE_H


#include <Eigen/Dense>


namespace ov_type {


    /**
     * @brief Base class for estimated variables.
     *
     * This class is used how variables are represented or updated (e.g., vectors or quaternions).
     * Each variable is defined by its error state size and its location in the covariance matrix.
     * We additionally require all sub-types to have a update procedure.
     */
    class Type {

    public:

        /**
        * @brief Default constructor for our Type
         *
        * @param size_ degrees of freedom of variable (i.e., the size of the error state)
        */
        Type(int size_) {
            _size = size_;
        }

        virtual ~Type() {};

        /**
        * @brief Sets id used to track location of variable in the filter covariance
         *
        * @param new_id entry in filter covariance corresponding to this variable
        */
        virtual void set_local_id(int new_id) {
            _id = new_id;
        }

        /**
        * @brief Access to variable id (i.e. its location in the covariance)
        */
        int id() {
            return _id;
        }

        /**
        * @brief Access to variable size (i.e. its error state size)
        */
        int size() {
            return _size;
        }

        /**
         * @brief Update variable due to perturbation of error state
         *
         * @param dx Perturbation used to update the variable through a defined "boxplus" operation
         */
        virtual void update(const Eigen::VectorXd dx) = 0;

        virtual void update_linp(const Eigen::VectorXd dx) =0;

        /**
         * @brief Access variable's estimate
         */
        virtual Eigen::MatrixXd value() const {
            return _value;
        }

        /**
         * @brief Access variable's first-estimate
         */
        virtual Eigen::MatrixXd fej() const {
            return _fej;
        }

        virtual Eigen::MatrixXd linp() const {
            return _linp;
        }
        





        /**
         * @brief Overwrite value of state's estimate
         * @param new_value New value that will overwrite state's value
         */
        virtual void set_value(const Eigen::MatrixXd new_value) {
            assert(_value.rows()==new_value.rows());
            assert(_value.cols()==new_value.cols());
            _value = new_value;
        }



        /**
         * @brief Overwrite value of first-estimate
         * @param new_value New value that will overwrite state's fej
         */
        virtual void set_fej(const Eigen::MatrixXd new_value) {
            assert(_fej.rows()==new_value.rows());
            assert(_fej.cols()==new_value.cols());
            _fej = new_value;
        }

        virtual void set_linp(const Eigen::MatrixXd new_value) {
            assert(_linp.rows()==new_value.rows());
            assert(_linp.cols()==new_value.cols());
            _linp = new_value;
        }

        /**
        * @brief Create a clone of this variable
        */
        virtual Type *clone() = 0;

        /**
         * @brief Determine if "check" is the same variable
         * If the passed variable is a sub-variable or the current variable this will return it.
         * Otherwise it will return a nullptr, meaning that it was unable to be found.
         *
         * @param check Type pointer to compare to
         */
        virtual Type *check_if_same_variable(const Type *check) {
            if (check == this) {
                return this;
            } else {
                return nullptr;
            }
        }

    protected:

        /// First-estimate
        Eigen::MatrixXd _fej;

        /// Current best estimate
        Eigen::MatrixXd _value;

        /// linearlize point
        Eigen::MatrixXd _linp;

        /// Location of error state in covariance
        int _id = -1;

        /// Dimension of error state
        int _size = -1;


    };

}

#endif //OV_TYPE_TYPE_BASE_H