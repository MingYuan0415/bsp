#ifndef __BSP_HOST_MT_LOG_H__
#define __BSP_HOST_MT_LOG_H__

#define DBG_INFO 3

static inline void _bsp_host_log(const char *format, ...)
{
    (void)format;
}

#define LOG_E(...) _bsp_host_log(__VA_ARGS__)
#define LOG_W(...) _bsp_host_log(__VA_ARGS__)
#define LOG_I(...) _bsp_host_log(__VA_ARGS__)
#define LOG_D(...) _bsp_host_log(__VA_ARGS__)

#endif /* __BSP_HOST_MT_LOG_H__ */
