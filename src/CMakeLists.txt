idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "."
        REQUIRES driver mbedtls bt  # 👈 关键：如果找不到蓝牙头文件，必须加上 bt
)