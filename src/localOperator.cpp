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

#include "hipBone.hpp"

void hipBone_t::LocalOperator(occa::memory &o_qL, occa::memory &o_Aq){


  if(mesh.NlocalGatherElements/2){
    localOperatorKernel(mesh.NlocalGatherElements/2,
			mesh.o_localGatherElementList,
			mesh.o_ggeo, mesh.o_D,
			lambda, o_qL, o_AqL);
  }


  if(mesh.NglobalGatherElements) {
    localOperatorKernel(mesh.NglobalGatherElements,
			mesh.o_globalGatherElementList,
			mesh.o_ggeo, mesh.o_D,
			lambda, o_qL, o_AqL);
  }

  if((mesh.NlocalGatherElements+1)/2){
    localOperatorKernel((mesh.NlocalGatherElements+1)/2,
			mesh.o_localGatherElementList+(mesh.NlocalGatherElements/2)*sizeof(dlong),
			mesh.o_ggeo, mesh.o_D,
			lambda, o_qL, o_AqL);
  }
}

