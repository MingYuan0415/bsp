#ifndef __SDKCONFIG_H__
#define __SDKCONFIG_H__

#define CONFIG_LV_COLOR_DEPTH 16

#if !defined(BSP_TEST_TE_SYNC_UNDEFINED) && \
    !defined(CONFIG_BSP_DISPLAY_TE_SYNC)
#define CONFIG_BSP_DISPLAY_TE_SYNC 1
#endif

#endif /* __SDKCONFIG_H__ */
