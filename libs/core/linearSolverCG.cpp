/*

The MIT License (MIT)

Copyright (c) 2020 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "linearSolver.hpp"

namespace libp {

constexpr int CG_BLOCKSIZE = 1024;

cg::cg(platform_t& _platform, dlong _N, dlong _Nhalo):
  linearSolver_t(_platform, _N, _Nhalo) {

  N = _N;
  dlong Ntotal = N + Nhalo;

  /*aux variables */
  dfloat *dummy = (dfloat *) calloc(Ntotal,sizeof(dfloat)); //need this to avoid uninitialized memory warnings
  o_p  = platform.malloc(Ntotal*sizeof(dfloat),dummy);
  o_Ap = platform.malloc(Ntotal*sizeof(dfloat),dummy);
  free(dummy);

  //pinned tmp buffer for reductions
  tmprdotr = (dfloat*) platform.hostMalloc(1*sizeof(dfloat),
                                          NULL, h_tmprdotr);
  o_tmprdotr = platform.malloc(1*sizeof(dfloat));

  /* build kernels */
  occa::properties kernelInfo = platform.props(); //copy base properties

  //add defines
  kernelInfo["defines/" "p_blockSize"] = (int)CG_BLOCKSIZE;

  // combined CG update and r.r kernel
  updateCGKernel1 = platform.buildKernel(HIPBONE_DIR "/libs/core/okl/linearSolverUpdateCG.okl",
                                "updateCG_1", kernelInfo);
  updateCGKernel2 = platform.buildKernel(HIPBONE_DIR "/libs/core/okl/linearSolverUpdateCG.okl",
                                "updateCG_2", kernelInfo);

  // separate kernels for 1. fused residual update with norm calculation and; 2. solution update
  updateCGKernel_r = platform.buildKernel(HIPBONE_DIR "/libs/core/okl/linearSolverUpdateCG.okl",
                                 "updateCG_r_atomic", kernelInfo);
}

int cg::Solve(solver_t& solver,
               occa::memory &o_x, occa::memory &o_r,
               const dfloat tol, const int MAXIT, const int verbose) {

  int rank = platform.rank;
  linAlg_t &linAlg = platform.linAlg();

  // register scalars
  dfloat rdotr1 = 0.0;
  dfloat rdotr2 = 0.0;
  dfloat alpha = 0.0, beta = 0.0, pAp = 0.0;
  dfloat rdotr = 0.0;

  // compute A*x
  solver.Operator(o_x, o_Ap);

  // subtract r = r - A*x
  linAlg.axpy(N, -1.f, o_Ap, 1.f, o_r);

  rdotr = linAlg.norm2(N, o_r, comm);
  rdotr = rdotr*rdotr;

  dfloat TOL = mymax(tol*tol*rdotr,tol*tol);

  if (verbose&&(rank==0))
    printf("CG: initial res norm %12.12f \n", sqrt(rdotr));

  int iter;
  for(iter=0;iter<MAXIT;++iter){

    //exit if tolerance is reached
    if(rdotr<=TOL) break;

    // r.r
    rdotr2 = rdotr1;
    rdotr1 = rdotr; //computed in UpdateCG

    beta = (iter==0) ? 0.0 : rdotr1/rdotr2;

    // p = r + beta*p
    linAlg.axpy(N, 1.f, o_r, beta, o_p);

    // A*p
    solver.Operator(o_p, o_Ap);

    // p.Ap
    pAp =  linAlg.innerProd(N, o_p, o_Ap, comm);

    alpha = rdotr1/pAp;

    //  x <= x + alpha*p
    //  r <= r - alpha*A*p
    //  dot(r,r)
    rdotr = UpdateCG(alpha, o_x, o_r);

    if (verbose&&(rank==0)) {
      if(rdotr<0)
        printf("WARNING CG: rdotr = %17.15lf\n", rdotr);

      printf("CG: it %d, r norm %12.12le, alpha = %le \n", iter+1, sqrt(rdotr), alpha);
    }
  }

  return iter;
}

dfloat cg::UpdateCG(const dfloat alpha, occa::memory &o_x, occa::memory &o_r){

  linAlg_t &linAlg = platform.linAlg();

  // r <= r - alpha*A*p
  // dot(r,r)
  int Nblocks = (N+CG_BLOCKSIZE-1)/CG_BLOCKSIZE;
  Nblocks = (Nblocks>CG_BLOCKSIZE) ? CG_BLOCKSIZE : Nblocks; //limit to CG_BLOCKSIZE entries

  // TODO: replace this with a device memset rather than using a kernel
  linAlg.setKernel(1, 0.0, o_tmprdotr);
  updateCGKernel_r(N, Nblocks, o_Ap, alpha, o_r, o_tmprdotr);
  o_tmprdotr.copyTo(tmprdotr, 1*sizeof(dfloat), 0, "async: true");

  occa::streamTag tag = platform.device.tagStream();

  // x = alpha * p + 1.0 * x
  linAlg.axpy(N, alpha, o_p, 1.0, o_x);

  platform.device.waitFor(tag);

  /*Compute all reduce while axpy is running*/
  dfloat rdotr1 = 0.0;
  MPI_Allreduce(tmprdotr, &rdotr1, 1, MPI_DFLOAT, MPI_SUM, comm);

  return rdotr1;
}

cg::~cg() {
  updateCGKernel1.free();
  updateCGKernel2.free();
  updateCGKernel_r.free();
}

} //namespace libp
