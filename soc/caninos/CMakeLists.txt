zephyr_include_directories(.)

zephyr_compile_options_ifndef(
  CONFIG_RISCV_GENERIC_TOOLCHAIN
  -march=riscv32imfc_zicsr
  )

zephyr_sources(
  init.S
  soc_irq.S
  puppy_irq.c
  puppy_timer.c
  puppy_event.c
  )

set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/flash_output.bin
)

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld CACHE INTERNAL "")

add_subdirectory(${CONFIG_SOC})
