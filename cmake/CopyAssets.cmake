function(dtnpc_copy_assets target asset_src_dir)
  set(asset_dst_dir "$<TARGET_FILE_DIR:${target}>/assets")

  add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${asset_src_dir}"
      "${asset_dst_dir}"
    COMMENT "Copy assets to: ${asset_dst_dir}"
  )
  add_dependencies(${target} copy_assets)
endfunction()
