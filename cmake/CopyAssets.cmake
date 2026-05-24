function(dtnpc_copy_assets target asset_src_dir)
  set(asset_dst_dir "$<TARGET_FILE_DIR:${target}>/assets")

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${asset_src_dir}"
      "${asset_dst_dir}"
    COMMENT "Copy assets to: ${asset_dst_dir}"
  )
endfunction()
