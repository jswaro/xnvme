// SPDX-FileCopyrightText: Samsung Electronics Co., Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <libxnvme.h>
#include <xnvme_be.h>

#include <errno.h>

#include <xnvme_dev.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <netinet/in.h>

#include <xnvme_be_nvmf.h>
#include <xnvme_be_nvmf_ctrlr.h>
#include <xnvme_be_nvmf_qpair.h>
#include <xnvme_be_nvmf_debug.h>
#include <xnvme_be_nvmf_rdma.h>

static struct xnvme_be_nvmf_ctrlr_ops g_xnvme_be_nvmf_rdma_ctrlr_ops;

#define _NVMF_RDMACM_DEBUG(fmt, ...) NVMF_DEBUG(NVMF_DEBUG_CATEGORY_RDMACM, fmt, ##__VA_ARGS__)
#define _NVMF_RDMACM_ERROR(fmt, ...) NVMF_ERROR(NVMF_DEBUG_CATEGORY_RDMACM, fmt, ##__VA_ARGS__)

static inline int
_rdma_resolve_addrinfo(struct xnvme_be_nvmf_ctrlr *ctrlr, const char *uri)
{
	struct xnvme_be_nvmf_rdma_ctrlr *rdma_ctrlr = TO_XNVME_NVMF_RDMA_CTRLR(ctrlr);
	char *cpy, *ip_addr = NULL, *port = NULL;
	int err;

	cpy = strdup(uri);
	if (!cpy) {
		_NVMF_RDMACM_ERROR("FAILED: strdup(), err: %d", errno);
		return -ENOMEM;
	}

	// TODO: change for IPv6 support
	ip_addr = strtok(cpy, ":");
	port = strtok(NULL, ":");

	err = rdma_getaddrinfo(ip_addr, port, NULL, &rdma_ctrlr->res);
	if (err) {
		_NVMF_RDMACM_ERROR("FAILED: rdma_getaddrinfo(), err: %d", err);
		goto failed_getaddrinfo;
	}
	_NVMF_RDMACM_DEBUG("INFO: Successfully retrieved address for transport: IP: %s, Port: %s",
			   ip_addr, port);
	_NVMF_RDMACM_DEBUG("INFO: Address family: %s",
			   rdma_ctrlr->res->ai_family == AF_INET ? "IPv4" : "IPv6");

	return 0;

failed_getaddrinfo:
	free(cpy);
	return err;
}

int
xnvme_be_nvmf_create_rdma_controller(struct xnvme_be_nvmf_ctrlr **ctrlr)
{
	struct xnvme_be_nvmf_rdma_ctrlr *rdma_ctrlr;

	rdma_ctrlr = calloc(1, sizeof(*rdma_ctrlr));
	if (!rdma_ctrlr) {
		_NVMF_RDMACM_ERROR("FAILED: calloc(), err: %d", errno);
		return -ENOMEM;
	}

	rdma_ctrlr->base.ops = &g_xnvme_be_nvmf_rdma_ctrlr_ops;
	*ctrlr = &rdma_ctrlr->base;

	return 0;
}

static inline int
_connect_rdma_controller(struct xnvme_be_nvmf_ctrlr *ctrlr, const char *uri)
{
	struct xnvme_be_nvmf_rdma_ctrlr *rdma_ctrlr = TO_XNVME_NVMF_RDMA_CTRLR(ctrlr);
	int err;

	err = _rdma_resolve_addrinfo(ctrlr, uri);
	if (err) {
		_NVMF_RDMACM_ERROR("FAILED: _rdma_resolve_addrinfo(), err: %d", err);
		return err;
	}

	rdma_ctrlr->selected = NULL;
	for (struct rdma_addrinfo *ai = rdma_ctrlr->res; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET) {
			_NVMF_RDMACM_DEBUG("INFO: Skipping unsupported address family: %d",
					   ai->ai_family);
			continue;
		}

		rdma_ctrlr->selected = ai;
		err = xnvme_be_nvmf_qpair_connect(ctrlr->admin_qpair);
		if (err) {
			rdma_ctrlr->selected = NULL;
			continue;
		}
		break;
	}

	if (err) {
		_NVMF_RDMACM_ERROR("FAILED: Could not connect to any suitable RDMA address");
		goto destroy_qp;
	}

	_NVMF_RDMACM_DEBUG("INFO: Successfully connected admin queue to remote controller");

	return 0;

destroy_qp:
	ctrlr->attached = 0;
	ctrlr->ctrlr_state = XNVME_NVMF_CTRLR_STATE_ERROR;
	if (ctrlr->admin_qpair) {
		xnvme_be_nvmf_qpair_destroy(ctrlr->admin_qpair);
		ctrlr->admin_qpair = NULL;
	}
	return -ENODEV;
}

static int
_disconnect_rdma_controller(struct xnvme_be_nvmf_ctrlr *ctrlr)
{
	int err;

	if (!ctrlr || !ctrlr->admin_qpair) {
		_NVMF_RDMACM_DEBUG("INFO: No admin_qpair to disconnect");
		return 0;
	}

	if (ctrlr->attached) {
		err = xnvme_be_nvmf_qpair_disconnect(ctrlr->admin_qpair);
		if (err) {
			_NVMF_RDMACM_ERROR("FAILED: xnvme_be_nvmf_disconnect_qpair(), err: %d",
					   err);
			return err;
		}

		xnvme_be_nvmf_qpair_destroy(ctrlr->admin_qpair);

		ctrlr->attached = 0;
	}

	// TODO: This requires proper handling.
	// xnvme_be_nvmf_destroy_qpair(rdma_ctrlr->base.sync_qpair);
	// free(rdma_ctrlr->base.sync_qpair);

	return 0;
}

static int
_destroy_rdma_controller(struct xnvme_be_nvmf_ctrlr *ctrlr)
{
	struct xnvme_be_nvmf_rdma_ctrlr *rdma_ctrlr = TO_XNVME_NVMF_RDMA_CTRLR(ctrlr);

	if (rdma_ctrlr->pd) {
		ibv_dealloc_pd(rdma_ctrlr->pd);
		rdma_ctrlr->pd = NULL;
	}

	if (rdma_ctrlr->res) {
		rdma_freeaddrinfo(rdma_ctrlr->res);
		rdma_ctrlr->res = NULL;
	}

	return 0;
}

/**
 * ctrlr_reg / ctrlr_dereg
 *
 * Thin wrapper around ibv_reg_mr()/ibv_dereg_mr() on the ctrlr's PD. The PD
 * is created lazily during the admin qpair's transport connect (see
 * design.md section 4), so it must exist by the time any caller registers
 * memory.
 */
static int
_reg_rdma_ctrlr(struct xnvme_be_nvmf_ctrlr *ctrlr, void *buf, size_t nbytes, void **handle,
		uint32_t *key)
{
	struct xnvme_be_nvmf_rdma_ctrlr *rdma_ctrlr = TO_XNVME_NVMF_RDMA_CTRLR(ctrlr);
	struct ibv_mr *mr;

	if (!rdma_ctrlr->pd) {
		_NVMF_RDMACM_ERROR("FAILED: no PD on controller");
		return -EINVAL;
	}

	mr = ibv_reg_mr(rdma_ctrlr->pd, buf, nbytes,
			IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
	if (!mr) {
		_NVMF_RDMACM_ERROR("FAILED: ibv_reg_mr(), err: %d", errno);
		return -errno;
	}

	*handle = mr;
	*key = mr->rkey;

	return 0;
}

static int
_dereg_rdma_ctrlr(struct xnvme_be_nvmf_ctrlr *XNVME_UNUSED(ctrlr), void *handle)
{
	struct ibv_mr *mr = handle;
	int err;

	if (!mr) {
		return 0;
	}

	err = ibv_dereg_mr(mr);
	if (err) {
		_NVMF_RDMACM_ERROR("FAILED: ibv_dereg_mr(), err: %d", err);
		return -err;
	}

	return 0;
}

static struct xnvme_be_nvmf_ctrlr_ops g_xnvme_be_nvmf_rdma_ctrlr_ops = {
	.connect = _connect_rdma_controller,
	.disconnect = _disconnect_rdma_controller,
	.destroy = _destroy_rdma_controller,

	.create_qpair = xnvme_be_nvmf_create_rdma_qpair,

	.ctrlr_reg = _reg_rdma_ctrlr,
	.ctrlr_dereg = _dereg_rdma_ctrlr,
};
