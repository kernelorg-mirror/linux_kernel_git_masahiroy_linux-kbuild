/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2019,2020,2022 NXP
 */

#ifndef __IMX_LDB_HELPER__
#define __IMX_LDB_HELPER__

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>

#define LDB_CH0_MODE_EN_TO_DI0		BIT(0)
#define LDB_CH0_MODE_EN_TO_DI1		(3 << 0)
#define LDB_CH0_MODE_EN_MASK		(3 << 0)
#define LDB_CH1_MODE_EN_TO_DI0		BIT(2)
#define LDB_CH1_MODE_EN_TO_DI1		(3 << 2)
#define LDB_CH1_MODE_EN_MASK		(3 << 2)
#define LDB_SPLIT_MODE_EN		BIT(4)
#define LDB_DATA_WIDTH_CH0_24		BIT(5)
#define LDB_BIT_MAP_CH0_JEIDA		BIT(6)
#define LDB_DATA_WIDTH_CH1_24		BIT(7)
#define LDB_BIT_MAP_CH1_JEIDA		BIT(8)
#define LDB_DI0_VS_POL_ACT_LOW		BIT(9)
#define LDB_DI1_VS_POL_ACT_LOW		BIT(10)

#define MAX_LDB_CHAN_NUM		2

enum ldb_channel_link_type {
	LDB_CH_SINGLE_LINK,
	LDB_CH_DUAL_LINK_EVEN_ODD_PIXELS,
	LDB_CH_DUAL_LINK_ODD_EVEN_PIXELS,
};

struct ldb;

struct ldb_channel {
	struct ldb *ldb;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct device_node *np;
	u32 chno;
	bool is_available;
	u32 in_bus_format;
	u32 out_bus_format;
	enum ldb_channel_link_type link_type;
};

struct ldb {
	struct regmap *regmap;
	struct device *dev;
	struct ldb_channel *channel[MAX_LDB_CHAN_NUM];
	unsigned int ctrl_reg;
	u32 ldb_ctrl;
	unsigned int available_ch_cnt;
};

#define bridge_to_ldb_ch(b)	container_of(b, struct ldb_channel, bridge)

static inline bool ldb_channel_is_single_link(struct ldb_channel *ldb_ch)
{
	return ldb_ch->link_type == LDB_CH_SINGLE_LINK;
}

static inline bool ldb_channel_is_split_link(struct ldb_channel *ldb_ch)
{
	return ldb_ch->link_type == LDB_CH_DUAL_LINK_EVEN_ODD_PIXELS ||
	       ldb_ch->link_type == LDB_CH_DUAL_LINK_ODD_EVEN_PIXELS;
}

static inline int ldb_bridge_atomic_check_helper(struct drm_bridge *bridge,
						 struct drm_bridge_state *bridge_state,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state)
{
	struct ldb_channel *ldb_ch = bridge->driver_private;

	ldb_ch->in_bus_format = bridge_state->input_bus_cfg.format;
	ldb_ch->out_bus_format = bridge_state->output_bus_cfg.format;

	return 0;
}

static inline void ldb_bridge_mode_set_helper(struct drm_bridge *bridge,
					      const struct drm_display_mode *mode,
					      const struct drm_display_mode *adjusted_mode)
{
	struct ldb_channel *ldb_ch = bridge->driver_private;
	struct ldb *ldb = ldb_ch->ldb;
	bool is_split = ldb_channel_is_split_link(ldb_ch);

	if (is_split)
		ldb->ldb_ctrl |= LDB_SPLIT_MODE_EN;

	switch (ldb_ch->out_bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		if (ldb_ch->chno == 0 || is_split)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24;
		if (ldb_ch->chno == 1 || is_split)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		if (ldb_ch->chno == 0 || is_split)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH0_24 |
					 LDB_BIT_MAP_CH0_JEIDA;
		if (ldb_ch->chno == 1 || is_split)
			ldb->ldb_ctrl |= LDB_DATA_WIDTH_CH1_24 |
					 LDB_BIT_MAP_CH1_JEIDA;
		break;
	}
}

static inline void ldb_bridge_enable_helper(struct drm_bridge *bridge)
{
	struct ldb_channel *ldb_ch = bridge->driver_private;
	struct ldb *ldb = ldb_ch->ldb;

	/*
	 * Platform specific bridge drivers should set ldb_ctrl properly
	 * for the enablement, so just write the ctrl_reg here.
	 */
	regmap_write(ldb->regmap, ldb->ctrl_reg, ldb->ldb_ctrl);
}

static inline void ldb_bridge_disable_helper(struct drm_bridge *bridge)
{
	struct ldb_channel *ldb_ch = bridge->driver_private;
	struct ldb *ldb = ldb_ch->ldb;
	bool is_split = ldb_channel_is_split_link(ldb_ch);

	if (ldb_ch->chno == 0 || is_split)
		ldb->ldb_ctrl &= ~LDB_CH0_MODE_EN_MASK;
	if (ldb_ch->chno == 1 || is_split)
		ldb->ldb_ctrl &= ~LDB_CH1_MODE_EN_MASK;

	regmap_write(ldb->regmap, ldb->ctrl_reg, ldb->ldb_ctrl);
}

static inline int ldb_bridge_attach_helper(struct drm_bridge *bridge,
					   enum drm_bridge_attach_flags flags)
{
	struct ldb_channel *ldb_ch = bridge->driver_private;
	struct ldb *ldb = ldb_ch->ldb;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_DEV_ERROR(ldb->dev,
			      "do not support creating a drm_connector\n");
		return -EINVAL;
	}

	if (!bridge->encoder) {
		DRM_DEV_ERROR(ldb->dev, "missing encoder\n");
		return -ENODEV;
	}

	return drm_bridge_attach(bridge->encoder,
				ldb_ch->next_bridge, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static inline int ldb_init_helper(struct ldb *ldb)
{
	struct device *dev = ldb->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;
	u32 i;

	ldb->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(ldb->regmap)) {
		ret = PTR_ERR(ldb->regmap);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	for_each_available_child_of_node(np, child) {
		struct ldb_channel *ldb_ch;

		ret = of_property_read_u32(child, "reg", &i);
		if (ret || i > MAX_LDB_CHAN_NUM - 1) {
			ret = -EINVAL;
			DRM_DEV_ERROR(dev,
				      "invalid channel node address: %u\n", i);
			of_node_put(child);
			return ret;
		}

		ldb_ch = ldb->channel[i];
		ldb_ch->ldb = ldb;
		ldb_ch->chno = i;
		ldb_ch->is_available = true;
		ldb_ch->np = child;

		ldb->available_ch_cnt++;
	}

	return 0;
}

static inline int ldb_find_next_bridge_helper(struct ldb *ldb)
{
	struct device *dev = ldb->dev;
	struct ldb_channel *ldb_ch;
	int ret, i;

	for (i = 0; i < MAX_LDB_CHAN_NUM; i++) {
		ldb_ch = ldb->channel[i];

		if (!ldb_ch->is_available)
			continue;

		ldb_ch->next_bridge = devm_drm_of_get_bridge(dev, ldb_ch->np,
							     1, 0);
		if (IS_ERR(ldb_ch->next_bridge)) {
			ret = PTR_ERR(ldb_ch->next_bridge);
			if (ret != -EPROBE_DEFER)
				DRM_DEV_ERROR(dev,
					      "failed to get next bridge: %d\n",
					      ret);
			return ret;
		}
	}

	return 0;
}

static inline void ldb_add_bridge_helper(struct ldb *ldb,
					 const struct drm_bridge_funcs *bridge_funcs)
{
	struct ldb_channel *ldb_ch;
	int i;

	for (i = 0; i < MAX_LDB_CHAN_NUM; i++) {
		ldb_ch = ldb->channel[i];

		if (!ldb_ch->is_available)
			continue;

		ldb_ch->bridge.driver_private = ldb_ch;
		ldb_ch->bridge.funcs = bridge_funcs;
		ldb_ch->bridge.of_node = ldb_ch->np;

		drm_bridge_add(&ldb_ch->bridge);
	}
}

static inline void ldb_remove_bridge_helper(struct ldb *ldb)
{
	struct ldb_channel *ldb_ch;
	int i;

	for (i = 0; i < MAX_LDB_CHAN_NUM; i++) {
		ldb_ch = ldb->channel[i];

		if (!ldb_ch->is_available)
			continue;

		drm_bridge_remove(&ldb_ch->bridge);
	}
}

#endif /* __IMX_LDB_HELPER__ */
