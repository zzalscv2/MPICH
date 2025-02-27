/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef TYPEREP_INTERNAL_H_INCLUDED
#define TYPEREP_INTERNAL_H_INCLUDED

#include "mpiimpl.h"

#define MPICH_DATATYPE_ENGINE_YAKSA    (1)
#define MPICH_DATATYPE_ENGINE_DATALOOP (2)

#if (MPICH_DATATYPE_ENGINE == MPICH_DATATYPE_ENGINE_YAKSA)

#include "yaksa.h"

extern yaksa_info_t MPII_yaksa_info_nogpu;

yaksa_type_t MPII_Typerep_get_yaksa_type(MPI_Datatype type);

static inline yaksa_info_t MPII_yaksa_get_info(MPL_pointer_attr_t * inattr,
                                               MPL_pointer_attr_t * outattr)
{
    if (inattr->type != MPL_GPU_POINTER_DEV && outattr->type != MPL_GPU_POINTER_DEV) {
        return MPII_yaksa_info_nogpu;
    }

    yaksa_info_t info;
    yaksa_info_create(&info);
#ifdef MPL_HAVE_GPU
#if MPL_HAVE_GPU == MPL_GPU_TYPE_CUDA
    int rc;
    yaksa_info_keyval_append(info, "yaksa_gpu_driver", "cuda", 5);
    rc = yaksa_info_keyval_append(info, "yaksa_cuda_inbuf_ptr_attr", &inattr->device_attr,
                                  sizeof(MPL_gpu_device_attr));
    MPIR_Assert(rc == 0);
    rc = yaksa_info_keyval_append(info, "yaksa_cuda_outbuf_ptr_attr", &outattr->device_attr,
                                  sizeof(MPL_gpu_device_attr));
    MPIR_Assert(rc == 0);
#elif MPL_HAVE_GPU == MPL_GPU_TYPE_ZE
    int rc;
    yaksa_info_keyval_append(info, "yaksa_gpu_driver", "ze", 3);
    rc = yaksa_info_keyval_append(info, "yaksa_ze_inbuf_ptr_attr", &inattr->device_attr,
                                  sizeof(MPL_gpu_device_attr));
    MPIR_Assert(rc == 0);
    rc = yaksa_info_keyval_append(info, "yaksa_ze_outbuf_ptr_attr", &outattr->device_attr,
                                  sizeof(MPL_gpu_device_attr));
    MPIR_Assert(rc == 0);
#endif
#endif
    return info;
}

static inline int MPII_yaksa_free_info(yaksa_info_t info)
{
    int rc = 0;
    if (info != MPII_yaksa_info_nogpu) {
        rc = yaksa_info_free(info);
    }
    return rc;
}

#else

int MPII_Typerep_convert_subarray(int ndims, MPI_Aint * array_of_sizes,
                                  MPI_Aint * array_of_subsizes, MPI_Aint * array_of_starts,
                                  int order, MPI_Datatype oldtype, MPI_Datatype * newtype);
int MPII_Typerep_convert_darray(int size, int rank, int ndims, const MPI_Aint * array_of_gsizes,
                                const int *array_of_distribs, const int *array_of_dargs,
                                const int *array_of_psizes, int order, MPI_Datatype oldtype,
                                MPI_Datatype * newtype);

#endif

#endif /* TYPEREP_INTERNAL_H_INCLUDED */
