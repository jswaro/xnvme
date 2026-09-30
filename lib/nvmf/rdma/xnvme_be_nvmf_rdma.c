// SPDX-FileCopyrightText: Samsung Electronics Co., Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>

#include <libxnvme.h>
#include <xnvme_be.h>

#include <xnvme_be_nvmf.h>
#include <xnvme_be_nvmf_qpair.h>
#include <xnvme_be_nvmf_transport.h>
#include <xnvme_be_nvmf_debug.h>
#include <xnvme_be_nvmf_rdma.h>

#define _NVMF_DATA_DEBUG(fmt, ...) NVMF_DEBUG(NVMF_DEBUG_CATEGORY_VERBS_DATA, fmt, ##__VA_ARGS__)
#define _NVMF_DATA_ERROR(fmt, ...) NVMF_ERROR(NVMF_DEBUG_CATEGORY_VERBS_DATA, fmt, ##__VA_ARGS__)

void
xnvme_be_nvmf_rdma_on_capsule_recv(struct xnvme_be_nvmf_qpair *qpair, void *buf, size_t len)
{
	struct xnvme_spec_cpl *cpl = buf;
	int err;

	if (len < sizeof(*cpl)) {
		_NVMF_DATA_ERROR("FAILED: short capsule, len: %zu", len);
		qpair->state = XNVME_NVMF_QPAIR_STATE_ERROR;
		return;
	}

	switch (qpair->state) {
	case XNVME_NVMF_QPAIR_STATE_CONNECTED:
	case XNVME_NVMF_QPAIR_STATE_READY:
		break;

	default:
		_NVMF_DATA_ERROR("FAILED: capsule in unexpected state: %d", qpair->state);
		break;
	}

	err = xnvme_be_nvmf_qpair_complete(qpair, cpl);
	if (err) {
		_NVMF_DATA_ERROR("FAILED: xnvme_be_nvmf_qpair_complete(), err: %d", err);
		qpair->state = XNVME_NVMF_QPAIR_STATE_ERROR;
		return;
	}

	_print_nvme_completion(cpl);
}

void
xnvme_be_nvmf_rdma_on_send_cmpl(struct xnvme_be_nvmf_qpair *qpair, void *buf, int status)
{
	(void)qpair;
	(void)buf;
	if (status)
		_NVMF_DATA_ERROR("FAILED: send completed with error: %d", status);
}

void
xnvme_be_nvmf_rdma_on_state_change(struct xnvme_be_nvmf_qpair *qpair,
				   enum xnvme_nvmf_qpair_state state, void *ctx)
{
	(void)ctx;

	switch (state) {
	case XNVME_NVMF_QPAIR_STATE_CONNECTED:
		break;
	case XNVME_NVMF_QPAIR_STATE_READY:
		// DONE
		break;
	case XNVME_NVMF_QPAIR_STATE_DISCONNECTED:
	case XNVME_NVMF_QPAIR_STATE_ERROR:
		break;

	default:
		break;
	}
}

struct xnvme_be_nvmf_transport g_xnvme_be_nvmf_rdma_transport = {
	.name = "rdma",
	.ops =
		{
			.create_ctrlr = xnvme_be_nvmf_create_rdma_controller,
		},
};
