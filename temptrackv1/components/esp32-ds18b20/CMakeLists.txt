#idf_component_register(SRCS "ds18b20.c"
#                    INCLUDE_DIRS "include")
set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_SRCS "ds18b20.c")
set(COMPONENT_REQUIRES "driver" "esp_timer")
set(COMPONENT_PRIV_REQUIRES "esp32-owb")
register_component()
