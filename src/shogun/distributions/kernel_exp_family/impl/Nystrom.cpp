/*
 * Copyright (c) The Shogun Machine Learning Toolbox
 * Written (w) 2016 Heiko Strathmann
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the Shogun Development Team.
 */

#include <shogun/lib/config.h>
#include <shogun/lib/SGMatrix.h>
#include <shogun/lib/SGVector.h>
#include <shogun/mathematics/eigen3.h>
#include <shogun/mathematics/Math.h>

#include "kernel/Base.h"
#include "Nystrom.h"

#include "Full.h"

using namespace shogun;
using namespace shogun::kernel_exp_family_impl;
using namespace Eigen;

Nystrom::Nystrom(SGMatrix<float64_t> data, SGMatrix<float64_t> basis,
		kernel::Base* kernel, float64_t lambda,
		float64_t lambda_l2) : Full(basis, kernel, lambda, 0.0, false)
{
	auto N = data.num_cols;

	SG_SINFO("Using m=%d of N=%d user provided basis points.\n",
			basis.num_cols, N);
	set_basis_and_data(basis, data);

	m_lambda_l2 = lambda_l2;
	m_basis_inds = SGVector<index_t>();
}

Nystrom::Nystrom(SGMatrix<float64_t> data, SGVector<index_t> basis_inds,
			kernel::Base* kernel, float64_t lambda,
			float64_t lambda_l2) : Full(data, kernel, lambda, 0.0, false)
{
	auto N = data.num_cols;

	SG_SINFO("Using m=%d of N=%d user provided subsampled data basis points.\n",
			basis_inds.vlen, N);

	m_basis_inds = basis_inds;
	auto basis = subsample_matrix_cols(basis_inds, data);
	set_basis_and_data(basis, data);

	m_lambda_l2 = lambda_l2;
}

Nystrom::Nystrom(SGMatrix<float64_t> data, index_t num_subsample_basis,
			kernel::Base* kernel, float64_t lambda,
			float64_t lambda_l2) : Full(data, kernel, lambda, 0.0, false)
{
	auto N = data.num_cols;

	SG_SINFO("Using m=%d of N=%d uniformly sub-sampled data as basis points.\n",
			num_subsample_basis, data.num_cols);

	m_basis_inds = choose_m_in_n(num_subsample_basis, N);
	auto basis = subsample_matrix_cols(m_basis_inds, data);

	set_basis_and_data(basis, data);
	m_lambda_l2 = lambda_l2;
}

index_t Nystrom::get_system_size() const
{
	auto m = get_num_basis();
	auto D = get_num_dimensions();

	return m*D;
}

SGMatrix<float64_t> Nystrom::subsample_G_mm_from_G_mn(const SGMatrix<float64_t>& G_mn) const
{
	auto system_size = get_system_size();
	auto D = get_num_dimensions();
	auto m = get_num_basis();

	SGMatrix<float64_t> G_mm(system_size, system_size);
	for (auto src_block=0; src_block<m; src_block++)
	{
		memcpy(G_mm.get_column_vector(src_block*D),
				G_mn.get_column_vector(m_basis_inds[src_block]*D),
				D*D*sizeof(float64_t)*m
				);
	}

	return G_mm;
}

SGMatrix<float64_t> Nystrom::compute_G_mn() const
{
	auto G_mn = m_kernel->dx_dy_all();
	return G_mn;
}

SGMatrix<float64_t> Nystrom::compute_G_mm()
{
	SG_SINFO("TODO: Avoid re-initializing the kernel matrix, make const\n");
	auto basis = m_basis;
	auto data = m_data;

	set_basis_and_data(basis, basis);
	auto G_mm = m_kernel->dx_dy_all();
	set_basis_and_data(basis, data);
	return G_mm;
}

SGVector<float64_t> Nystrom::solve_system(const SGMatrix<float64_t>& system_matrix,
		const SGVector<float64_t>& system_vector) const
{
	auto system_size = get_system_size();
	auto eigen_system_vector = Map<VectorXd>(system_vector.vector, system_size);
	auto eigen_system_matrix = Map<MatrixXd>(system_matrix.matrix,
			system_matrix.num_rows, system_matrix.num_cols);

	SGVector<float64_t> result(system_size);
	auto eigen_result = Map<VectorXd>(result.vector, system_size);

	// attempt fast solvers first and use stable fall-back option otherwise
	bool solve_success=false;
	if (m_lambda_l2>0)
	{
		// this has smalles Eigenvalues bounded away form zero, so can use fast LLT
		SG_SINFO("Solving with LLT.\n");
		auto solver = LLT<MatrixXd>();
		solver.compute(eigen_system_matrix);
		if (solver.info() != Eigen::Success)
		{
			SG_SWARNING("Numerical problems computing LLT.\n");
			SG_SINFO("Solving with LDLT.\n");
			auto solver2 = LDLT<MatrixXd>();
			solver2.compute(eigen_system_matrix);
			if (solver2.info() != Eigen::Success)
			{
				SG_SWARNING("Numerical problems computing LDLT.\n");
			}
			else
			{
				SG_SINFO("Constructing solution.\n");
				eigen_result = -solver.solve(eigen_system_vector);
				solve_success=true;
			}
		}
		else
		{
			SG_SINFO("Constructing solution.\n");
			eigen_result = -solver.solve(eigen_system_vector);
			solve_success=true;
		}
	}
	else
	{
		SG_SINFO("Solving with HouseholderQR.\n");
		SG_SWARNING("TODO: Compare QR and self-adjoint-pseudo-inverse.\n");
		auto solver = HouseholderQR<MatrixXd>();
		solver.compute(eigen_system_matrix);

		SG_SINFO("Constructing solution.\n");
		eigen_result = -solver.solve(eigen_system_vector);
		solve_success=true;
	}

	if (!solve_success)
	{
		SG_SINFO("Solving with self-adjoint Eigensolver based pseudo-inverse.\n");
		auto G_dagger = pinv_self_adjoint(system_matrix);
		Map<MatrixXd> eigen_G_dagger(G_dagger.matrix, system_size, system_size);

		SG_SINFO("Constructing solution.\n");
		eigen_result = -eigen_G_dagger * eigen_system_vector;
	}

	return result;
}

void Nystrom::fit()
{
	auto D = get_num_dimensions();
	auto N = get_num_data();
	auto ND = N*D;
	auto system_size = get_system_size();

	SGMatrix<float64_t> system_matrix(system_size, system_size);
	Map<MatrixXd> eigen_system_matrix(system_matrix.matrix, system_size, system_size);

	SG_SINFO("Computing h.\n");
	auto h = compute_h();
	auto eigen_h=Map<VectorXd>(h.vector, system_size);

	SG_SINFO("Computing kernel Hessians between basis and data.\n");
	auto G_mn = compute_G_mn();
	Map<MatrixXd> eigen_G_mn(G_mn.matrix, system_size, ND);
	eigen_system_matrix = eigen_G_mn*eigen_G_mn.adjoint() / N;
	if (m_lambda>0)
	{
		if (basis_is_subsampled_data())
		{
			SG_SINFO("Block sub-sampling kernel Hessians for basis.\n");
			auto G_mm = subsample_G_mm_from_G_mn(G_mn);
			Map<MatrixXd> eigen_G_mm(G_mm.matrix, system_size, system_size);
			eigen_system_matrix+=m_lambda*eigen_G_mm;
		}
		else
		{
			SG_SINFO("Computing kernel Hessians for basis.\n");
			auto G_mm = compute_G_mm();
			Map<MatrixXd> eigen_G_mm(G_mm.matrix, system_size, system_size);
			eigen_system_matrix+=m_lambda*eigen_G_mm;
		}
	}

	if (m_lambda_l2>0.0)
		eigen_system_matrix.diagonal().array() += m_lambda_l2;

	m_beta = solve_system(system_matrix, h);
}

SGMatrix<float64_t> Nystrom::pinv_self_adjoint(const SGMatrix<float64_t>& A)
{
	// based on the snippet from
	// http://eigen.tuxfamily.org/index.php?title=FAQ#Is_there_a_method_to_compute_the_.28Moore-Penrose.29_pseudo_inverse_.3F
	// modified using eigensolver for psd problems
	auto m=A.num_rows;
	ASSERT(A.num_cols == m);
	auto eigen_A=Map<MatrixXd>(A.matrix, m, m);

	SelfAdjointEigenSolver<MatrixXd> solver(eigen_A);
	auto s = solver.eigenvalues();
	auto V = solver.eigenvectors();

	// tol = eps⋅max(m,n) * max(singularvalues)
	// this is done in numpy/Octave & co
	// c.f. https://en.wikipedia.org/wiki/Moore%E2%80%93Penrose_pseudoinverse#Singular_value_decomposition_.28SVD.29
	float64_t pinv_tol = CMath::MACHINE_EPSILON * m * s.maxCoeff();

	VectorXd inv_s(m);
	for (auto i=0; i<m; i++)
	{
		if (s(i) > pinv_tol)
			inv_s(i)=1.0/s(i);
		else
			inv_s(i)=0;
	}

	SGMatrix<float64_t> A_pinv(m, m);
	Map<MatrixXd> eigen_pinv(A_pinv.matrix, m, m);
	eigen_pinv = (V*inv_s.asDiagonal()*V.transpose());

	return A_pinv;
}

SGVector<index_t> Nystrom::choose_m_in_n(index_t m, index_t n, bool sorted)
{
	SGVector<index_t> permutation(n);
	permutation.range_fill();
	CMath::permute(permutation);
	auto chosen = SGVector<index_t>(m);

	memcpy(chosen.vector, permutation.vector, sizeof(index_t)*m);

	// in order to have more sequential data reads
	if (sorted)
		CMath::qsort(chosen.vector, m);

	return chosen;
}

SGMatrix<float64_t> Nystrom::subsample_matrix_cols(const SGVector<index_t>& col_inds,
		const SGMatrix<float64_t>& mat)
{
	auto D = mat.num_rows;
	auto N_new = col_inds.vlen;
	auto subsampled=SGMatrix<float64_t>(D, N_new);
	for (auto i=0; i<N_new; i++)
	{
		memcpy(subsampled.get_column_vector(i), mat.get_column_vector(col_inds[i]),
				sizeof(float64_t)*D);
	}

	return subsampled;
}

// TODO shouldnt be overloaded to get rid of xi
float64_t Nystrom::log_pdf(index_t idx_test) const
{
	auto D = get_num_dimensions();
	auto m = get_num_basis();

	float64_t beta_sum = 0;

	Map<VectorXd> eigen_beta(m_beta.vector, m*D);
	for (auto idx_a=0; idx_a<m; idx_a++)
	{
		auto grad_x_xa = m_kernel->dx(idx_a, idx_test);
		Map<VectorXd> eigen_grad_xa(grad_x_xa.vector, D);
		beta_sum += eigen_grad_xa.dot(eigen_beta.segment(idx_a*D, D));
	}

	return beta_sum;
}

// TODO shouldnt be overloaded to get rid of xi
SGVector<float64_t> Nystrom::grad(index_t idx_test) const
{
	auto D = get_num_dimensions();
	auto m = get_num_basis();

	SGVector<float64_t> beta_sum_grad(D);
	Map<VectorXd> eigen_beta_sum_grad(beta_sum_grad.vector, D);
	eigen_beta_sum_grad.array() = VectorXd::Zero(D);

	Map<VectorXd> eigen_beta(m_beta.vector, m*D);
	for (auto a=0; a<m; a++)
	{
		auto left_arg_hessian = m_kernel->dx_i_dx_j(a, idx_test);
		Map<MatrixXd> eigen_left_arg_hessian(left_arg_hessian.matrix, D, D);
		eigen_beta_sum_grad -= eigen_left_arg_hessian*eigen_beta.segment(a*D, D).matrix();
	}

	return beta_sum_grad;
}

// TODO shouldnt be overloaded to get rid of xi
SGVector<float64_t> Nystrom::hessian_diag(index_t idx_test) const
{
	REQUIRE(!m_base_measure_cov_ridge, "Base measure not implemented for Hessian.\n");

	// Note: code modifed from full hessian case
	auto m = get_num_basis();
	auto D = get_num_dimensions();

	SGVector<float64_t> beta_sum_hessian_diag(D);

	Map<VectorXd> eigen_beta_sum_hessian_diag(beta_sum_hessian_diag.vector, D);

	eigen_beta_sum_hessian_diag = VectorXd::Zero(D);

	Map<VectorXd> eigen_beta(m_beta.vector, m*D);

	for (auto a=0; a<m; a++)
	{
		SGVector<float64_t> beta_a(eigen_beta.segment(a*D, D).data(), D, false);
		for (auto i=0; i<D; i++)
		{
			eigen_beta_sum_hessian_diag[i] += m_kernel->dx_i_dx_j_dx_k_dot_vec_component(
					a, idx_test, beta_a, i, i);
		}
	}

	return beta_sum_hessian_diag;
}
