idf_component_register(SRCS "cwd_emulate.cpp"
                    INCLUDE_DIRS .
                    PRIV_REQUIRES extrstream
#                    REQUIRES console driver sdmmc spi_flash fatfs cxx utils
                    REQUIRES sdmmc fatfs utils
		    )

