#ifndef _INTERNAL_XNVME_BE_NVMF_TRANSPORT_H
#define _INTERNAL_XNVME_BE_NVMF_TRANSPORT_H

/**
 * NVMe-oF transport descriptor and ops table.
 *
 * Forward declarations only, so this header pulls in no ctrlr/qpair object
 * definitions. Each transport (RDMA, TCP, ...) provides one
 * `struct xnvme_be_nvmf_transport` instance, cached by the core.
 */

struct xnvme_be_nvmf_ctrlr;
struct xnvme_be_nvmf_qpair;

struct xnvme_be_nvmf_transport_ops {
	int (*create_ctrlr)(struct xnvme_be_nvmf_ctrlr **ctrlr);
};

struct xnvme_be_nvmf_transport {
	const char *name;
	struct xnvme_be_nvmf_transport_ops ops;
};

#endif /* _INTERNAL_XNVME_BE_NVMF_TRANSPORT_H */
