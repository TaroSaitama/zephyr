/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost);

#define QUEUE_READY_HANDLER_NAME(node_name) UTIL_CAT(node_name, _queue_ready)

#define QUEUE_READY_HANDLER_PROTODECL(sym)                                                         \
	void QUEUE_READY_HANDLER_NAME(sym)(const struct device *dev, uint16_t qid, void *data);

#define DECL_HANDLER(inst, x, y)                                                                   \
	QUEUE_READY_HANDLER_PROTODECL(DT_NODE_FULL_NAME_UNQUOTED(DT_INST(inst, xen_vhost_mmio)))

#define REGISTER_HANDLER(inst, x, y)                                                               \
	{                                                                                          \
		const struct device *dev = DEVICE_DT_GET(DT_INST(inst, xen_vhost_mmio));           \
		register_handler(dev, QUEUE_READY_HANDLER_NAME(DT_NODE_FULL_NAME_UNQUOTED(         \
					      DT_INST(inst, xen_vhost_mmio))));                    \

struct vringh vrh_inst;

static void vringh_kick_handler(struct vringh *vrh)
{
	LOG_DBG("%s: queue_id=%lu", __func__, vrh->queue_id);
	uint16_t head;

	while (true) {
		int ret = vringh_getdesc(vrh, &riov, &wiov, &head);
		printk("Get a descriptor\n");
		printk("ret=%d\n", ret);

		if (ret < 0) {
			LOG_ERR("vringh_getdesc failed: %d", ret);
			return;
		}

		if (ret == 0) {
			return;
		}
		printk("Begin processing a request\n");
		/* Process writable iovecs */
		for (uint32_t s = 0; s < wiov.used; s++) {
			uint8_t *dst = wiov.iov[s].iov_base;
			uint32_t len = wiov.iov[s].iov_len;

			LOG_DBG("%s: addr=%p len=%u", __func__, dst, len);
                        printk("%s: addr=%p len=%u \n", __func__, dst, len);
			for (uint32_t i = 0; i < len; i++) {
				sys_write8(i, (mem_addr_t)&dst[i]);
			}
		}

		barrier_dmem_fence_full();

		uint32_t total_len = 0;

		for (uint32_t i = 0; i < wiov.used; i++) {
			total_len += wiov.iov[i].iov_len;
		}

		vringh_complete(vrh, head, total_len);
                printk("Notify of completion\n");

		if (vringh_need_notify(vrh) > 0) {
			vringh_notify(vrh);
		}

		/* Reset iovecs for next iteration */
		vringh_iov_reset(&riov);
		vringh_iov_reset(&wiov);
    }
}

DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(xen_vhost_mmio, DECL_HANDLER, ())

int register_handler(const struct device *dev,
		     void (*handler)(const struct device *, uint16_t, void *))
{
	if (!device_is_ready(dev)) {
		LOG_ERR("VHost device %s not ready", dev->name);
		return -ENODEV;
	}

	LOG_INF("VHost device %s ready", dev->name);
	vhost_register_virtq_ready_cb(dev, handler, (void *)dev);

	return 0;
}

int main(void)
{
	DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(xen_vhost_mmio, REGISTER_HANDLER, ())

	LOG_INF("VHost sample application started, waiting for guest connections...");
	k_sleep(K_FOREVER);

	return 0;
}
