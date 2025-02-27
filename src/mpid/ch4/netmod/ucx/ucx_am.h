/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef UCX_AM_H_INCLUDED
#define UCX_AM_H_INCLUDED

#include "ucx_impl.h"

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_am_isend_callback(void *request, ucs_status_t status)
{
    MPIDI_UCX_ucp_request_t *ucp_request = (MPIDI_UCX_ucp_request_t *) request;
    MPIR_Request *req = ucp_request->req;
    int handler_id = req->dev.ch4.am.netmod_am.ucx.handler_id;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_UCX_AM_ISEND_CALLBACK);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_UCX_AM_ISEND_CALLBACK);

    MPIR_gpu_free_host(req->dev.ch4.am.netmod_am.ucx.pack_buffer);
    req->dev.ch4.am.netmod_am.ucx.pack_buffer = NULL;
    MPIDIG_global.origin_cbs[handler_id] (req);
    ucp_request->req = NULL;

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_UCX_AM_ISEND_CALLBACK);
}

MPL_STATIC_INLINE_PREFIX void MPIDI_UCX_am_send_callback(void *request, ucs_status_t status)
{
    MPIDI_UCX_ucp_request_t *ucp_request = (MPIDI_UCX_ucp_request_t *) request;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_UCX_AM_SEND_CALLBACK);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_UCX_AM_SEND_CALLBACK);

    MPL_free(ucp_request->buf);
    ucp_request->buf = NULL;
    ucp_request_release(ucp_request);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_UCX_AM_SEND_CALLBACK);
}


MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_isend(int rank,
                                               MPIR_Comm * comm,
                                               int handler_id,
                                               const void *am_hdr,
                                               MPI_Aint am_hdr_sz,
                                               const void *data,
                                               MPI_Aint count, MPI_Datatype datatype,
                                               MPIR_Request * sreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_UCX_ucp_request_t *ucp_request;
    ucp_ep_h ep;
    uint64_t ucx_tag;
    char *send_buf;
    size_t data_sz;
    MPIDI_UCX_am_header_t ucx_hdr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_ISEND);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_ISEND);

    MPIDI_Datatype_check_size(datatype, count, data_sz);

    ep = MPIDI_UCX_COMM_TO_EP(comm, rank, 0, 0);
    ucx_tag = MPIDI_UCX_init_tag(0, MPIR_Process.comm_world->rank, MPIDI_UCX_AM_TAG);

    /* initialize our portion of the hdr */
    ucx_hdr.handler_id = handler_id;
    ucx_hdr.data_sz = data_sz;

    MPIR_gpu_malloc_host((void **) &send_buf, data_sz + am_hdr_sz + sizeof(ucx_hdr));
    MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
    MPIR_Memcpy(send_buf + sizeof(ucx_hdr), am_hdr, am_hdr_sz);

    MPI_Aint actual_pack_bytes;
    mpi_errno =
        MPIR_Typerep_pack(data, count, datatype, 0, send_buf + am_hdr_sz + sizeof(ucx_hdr),
                          data_sz, &actual_pack_bytes);
    MPIR_ERR_CHECK(mpi_errno);
    MPIR_Assert(actual_pack_bytes == data_sz);

    ucp_request = (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nb(ep, send_buf,
                                                              data_sz + am_hdr_sz + sizeof(ucx_hdr),
                                                              ucp_dt_make_contig(1), ucx_tag,
                                                              &MPIDI_UCX_am_isend_callback);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    /* send is done. free all resources and complete the request */
    if (ucp_request == NULL) {
        MPIR_gpu_free_host(send_buf);
        MPIDIG_global.origin_cbs[handler_id] (sreq);
        goto fn_exit;
    }

    /* set the ch4r request inside the UCP request */
    sreq->dev.ch4.am.netmod_am.ucx.pack_buffer = send_buf;
    sreq->dev.ch4.am.netmod_am.ucx.handler_id = handler_id;
    ucp_request->req = sreq;
    ucp_request_release(ucp_request);


  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_ISEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_isendv(int rank,
                                                MPIR_Comm * comm,
                                                int handler_id,
                                                struct iovec *am_hdr,
                                                size_t iov_len,
                                                const void *data,
                                                MPI_Aint count, MPI_Datatype datatype,
                                                MPIR_Request * sreq)
{
    int mpi_errno = MPI_SUCCESS;
    size_t am_hdr_sz = 0, i;
    MPIDI_UCX_ucp_request_t *ucp_request;
    ucp_ep_h ep;
    uint64_t ucx_tag;
    char *send_buf;
    size_t data_sz;
    MPIDI_UCX_am_header_t ucx_hdr;


    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_ISENDV);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_ISENDV);

    ep = MPIDI_UCX_COMM_TO_EP(comm, rank, 0, 0);
    ucx_tag = MPIDI_UCX_init_tag(0, MPIR_Process.comm_world->rank, MPIDI_UCX_AM_TAG);

    MPIDI_Datatype_check_size(datatype, count, data_sz);
    for (i = 0; i < iov_len; i++) {
        am_hdr_sz += am_hdr[i].iov_len;
    }

    /* copy headers to send buffer */
    MPIR_gpu_malloc_host((void **) &send_buf, data_sz + am_hdr_sz + sizeof(ucx_hdr));
    ucx_hdr.handler_id = handler_id;
    ucx_hdr.data_sz = data_sz;
    MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
    am_hdr_sz = 0;
    for (i = 0; i < iov_len; i++) {
        MPIR_Memcpy(send_buf + am_hdr_sz + sizeof(ucx_hdr), am_hdr[i].iov_base, am_hdr[i].iov_len);
        am_hdr_sz += am_hdr[i].iov_len;
    }

    MPI_Aint actual_pack_bytes;
    mpi_errno =
        MPIR_Typerep_pack(data, count, datatype, 0, send_buf + sizeof(ucx_hdr) + am_hdr_sz,
                          data_sz, &actual_pack_bytes);
    MPIR_ERR_CHECK(mpi_errno);
    MPIR_Assert(actual_pack_bytes == data_sz);

    ucp_request = (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nb(ep, send_buf,
                                                              data_sz + am_hdr_sz + sizeof(ucx_hdr),
                                                              ucp_dt_make_contig(1), ucx_tag,
                                                              &MPIDI_UCX_am_isend_callback);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    /* send is done. free all resources and complete the request */
    if (ucp_request == NULL) {
        MPL_gpu_free_host(send_buf);
        MPIDIG_global.origin_cbs[handler_id] (sreq);
        goto fn_exit;
    }

    /* set the ch4r request inside the UCP request */
    sreq->dev.ch4.am.netmod_am.ucx.pack_buffer = send_buf;
    sreq->dev.ch4.am.netmod_am.ucx.handler_id = handler_id;
    ucp_request->req = sreq;
    ucp_request_release(ucp_request);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_ISENDV);
    return mpi_errno;

  fn_fail:
    goto fn_exit;

}


MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_isend_reply(MPIR_Context_id_t context_id,
                                                     int src_rank,
                                                     int handler_id,
                                                     const void *am_hdr,
                                                     MPI_Aint am_hdr_sz,
                                                     const void *data, MPI_Aint count,
                                                     MPI_Datatype datatype, MPIR_Request * sreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_UCX_ucp_request_t *ucp_request;
    ucp_ep_h ep;
    ucp_datatype_t dt;
    uint64_t ucx_tag;
    char *send_buf;
    void *send_buf_p;
    size_t data_sz;
    size_t total_sz;
    MPI_Aint dt_true_lb;
    MPIR_Datatype *dt_ptr;
    int dt_contig;
    MPIDI_UCX_am_header_t ucx_hdr;
    MPIR_Comm *use_comm;
    ucp_dt_iov_t *iov = sreq->dev.ch4.am.netmod_am.ucx.iov;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_ISEND_REPLY);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_ISEND_REPLY);

    use_comm = MPIDIG_context_id_to_comm(context_id);
    ep = MPIDI_UCX_COMM_TO_EP(use_comm, src_rank, 0, 0);
    ucx_tag = MPIDI_UCX_init_tag(0, MPIR_Process.comm_world->rank, MPIDI_UCX_AM_TAG);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    MPL_pointer_attr_t attr;
    MPIR_GPU_query_pointer_attr(data, &attr);
    if (attr.type == MPL_GPU_POINTER_DEV) {
        /* Force packing of GPU buffer in host memory */
        dt_contig = 0;
    }

    /* initialize our portion of the hdr */
    ucx_hdr.handler_id = handler_id;
    ucx_hdr.data_sz = data_sz;

    if (dt_contig) {
        send_buf = MPL_malloc(sizeof(ucx_hdr) + am_hdr_sz, MPL_MEM_BUFFER);
        MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
        MPIR_Memcpy(send_buf + sizeof(ucx_hdr), am_hdr, am_hdr_sz);

        iov[0].buffer = send_buf;
        iov[0].length = sizeof(ucx_hdr) + am_hdr_sz;
        iov[1].buffer = (char *) data + dt_true_lb;
        iov[1].length = data_sz;

        send_buf_p = iov;
        dt = ucp_dt_make_iov();
        total_sz = 2;
    } else {
        /* FIXME: currently we always do packing, also for high density types. However,
         * we should not do packing unless needed. Also, for large low-density types
         * we should not allocate the entire buffer and do the packing at once. */
        /* TODO: (1) Skip packing for high-density datatypes;
         *       (2) Pipeline allocation for low-density datatypes; */
        /* FIXME: allocating a GPU registered host buffer adds some additional overhead.
         * However, once the new buffer pool infrastructure is setup, we would simply be
         * allocating a buffer from the pool, so whether it's a regular malloc buffer or a GPU
         * registered buffer should be equivalent with respect to performance. */
        MPIR_gpu_malloc_host((void **) &send_buf, data_sz + am_hdr_sz + sizeof(ucx_hdr));

        MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
        MPIR_Memcpy(send_buf + sizeof(ucx_hdr), am_hdr, am_hdr_sz);

        MPI_Aint actual_pack_bytes;
        mpi_errno =
            MPIR_Typerep_pack(data, count, datatype, 0, send_buf + am_hdr_sz + sizeof(ucx_hdr),
                              data_sz, &actual_pack_bytes);
        MPIR_ERR_CHECK(mpi_errno);
        MPIR_Assert(actual_pack_bytes == data_sz);

        send_buf_p = send_buf;
        dt = ucp_dt_make_contig(1);
        total_sz = data_sz + am_hdr_sz + sizeof(ucx_hdr);
    }
    ucp_request =
        (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nb(ep,
                                                    send_buf_p, total_sz, dt, ucx_tag,
                                                    &MPIDI_UCX_am_isend_callback);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    /* send is done. free all resources and complete the request */
    if (ucp_request == NULL) {
        MPIR_gpu_free_host(send_buf);
        MPIDIG_global.origin_cbs[handler_id] (sreq);
        goto fn_exit;
    }

    /* set the ch4r request inside the UCP request */
    sreq->dev.ch4.am.netmod_am.ucx.pack_buffer = send_buf;
    sreq->dev.ch4.am.netmod_am.ucx.handler_id = handler_id;
    ucp_request->req = sreq;
    ucp_request_release(ucp_request);

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_ISEND_REPLY);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_NM_am_hdr_max_sz(void)
{
    return (MPIDI_UCX_MAX_AM_EAGER_SZ - sizeof(MPIDI_UCX_am_header_t));
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_NM_am_eager_limit(void)
{
    return (MPIDI_UCX_MAX_AM_EAGER_SZ - sizeof(MPIDI_UCX_am_header_t));
}

MPL_STATIC_INLINE_PREFIX MPI_Aint MPIDI_NM_am_eager_buf_limit(void)
{
    return MPIDI_UCX_MAX_AM_EAGER_SZ;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_send_hdr(int rank,
                                                  MPIR_Comm * comm,
                                                  int handler_id, const void *am_hdr,
                                                  MPI_Aint am_hdr_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_UCX_ucp_request_t *ucp_request;
    ucp_ep_h ep;
    uint64_t ucx_tag;
    char *send_buf;
    MPIDI_UCX_am_header_t ucx_hdr;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_SEND_HDR);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_SEND_HDR);

    ep = MPIDI_UCX_COMM_TO_EP(comm, rank, 0, 0);
    ucx_tag = MPIDI_UCX_init_tag(0, MPIR_Process.comm_world->rank, MPIDI_UCX_AM_TAG);

    /* initialize our portion of the hdr */
    ucx_hdr.handler_id = handler_id;
    ucx_hdr.data_sz = 0;

    /* just pack and send for now */
    send_buf = MPL_malloc(am_hdr_sz + sizeof(ucx_hdr), MPL_MEM_BUFFER);
    MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
    MPIR_Memcpy(send_buf + sizeof(ucx_hdr), am_hdr, am_hdr_sz);

    ucp_request = (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nb(ep, send_buf,
                                                              am_hdr_sz + sizeof(ucx_hdr),
                                                              ucp_dt_make_contig(1), ucx_tag,
                                                              &MPIDI_UCX_am_send_callback);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    if (ucp_request == NULL) {
        /* inject is done */
        MPL_free(send_buf);
    } else {
        ucp_request->buf = send_buf;
    }

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_SEND_HDR);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_send_hdr_reply(MPIR_Context_id_t context_id,
                                                        int src_rank,
                                                        int handler_id, const void *am_hdr,
                                                        MPI_Aint am_hdr_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_UCX_ucp_request_t *ucp_request;
    ucp_ep_h ep;
    uint64_t ucx_tag;
    char *send_buf;
    MPIDI_UCX_am_header_t ucx_hdr;
    MPIR_Comm *use_comm;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_SEND_HDR_REPLY);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_SEND_HDR_REPLY);

    use_comm = MPIDIG_context_id_to_comm(context_id);
    ep = MPIDI_UCX_COMM_TO_EP(use_comm, src_rank, 0, 0);
    ucx_tag = MPIDI_UCX_init_tag(0, MPIR_Process.comm_world->rank, MPIDI_UCX_AM_TAG);

    /* initialize our portion of the hdr */
    ucx_hdr.handler_id = handler_id;
    ucx_hdr.data_sz = 0;

    /* just pack and send for now */
    send_buf = MPL_malloc(am_hdr_sz + sizeof(ucx_hdr), MPL_MEM_BUFFER);
    MPIR_Memcpy(send_buf, &ucx_hdr, sizeof(ucx_hdr));
    MPIR_Memcpy(send_buf + sizeof(ucx_hdr), am_hdr, am_hdr_sz);
    ucp_request = (MPIDI_UCX_ucp_request_t *) ucp_tag_send_nb(ep, send_buf,
                                                              am_hdr_sz + sizeof(ucx_hdr),
                                                              ucp_dt_make_contig(1), ucx_tag,
                                                              &MPIDI_UCX_am_send_callback);
    MPIDI_UCX_CHK_REQUEST(ucp_request);

    if (ucp_request == NULL) {
        /* inject is done */
        MPL_free(send_buf);
    } else {
        ucp_request->buf = send_buf;
    }

  fn_exit:
    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_SEND_HDR_REPLY);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

MPL_STATIC_INLINE_PREFIX bool MPIDI_NM_am_check_eager(MPI_Aint am_hdr_sz, MPI_Aint data_sz,
                                                      const void *data, MPI_Aint count,
                                                      MPI_Datatype datatype, MPIR_Request * sreq)
{
    return (am_hdr_sz + data_sz) <= (MPIDI_UCX_MAX_AM_EAGER_SZ - sizeof(MPIDI_UCX_am_header_t));
}

MPL_STATIC_INLINE_PREFIX int MPIDI_NM_am_recv(MPIR_Request * rreq)
{
    int ret = MPI_SUCCESS;

    MPIR_FUNC_VERBOSE_STATE_DECL(MPID_STATE_MPIDI_NM_AM_RECV);
    MPIR_FUNC_VERBOSE_ENTER(MPID_STATE_MPIDI_NM_AM_RECV);

    MPIR_FUNC_VERBOSE_EXIT(MPID_STATE_MPIDI_NM_AM_RECV);
    return ret;
}

#endif /* UCX_AM_H_INCLUDED */
