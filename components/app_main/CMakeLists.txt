idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "." 
                    "../common"                    
                    "../use_wifi"
                    REQUIRES json nvs_flash bt mqtt)
