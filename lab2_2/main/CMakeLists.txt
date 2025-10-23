idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver           # for i2c_master APIs
    PRIV_REQUIRES esp_timer   # because main.c includes esp_timer.h
)
