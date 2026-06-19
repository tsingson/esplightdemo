idf_component_register(SRCS "main.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES nimble) # 明确声明依赖 NimBLE 蓝牙组件