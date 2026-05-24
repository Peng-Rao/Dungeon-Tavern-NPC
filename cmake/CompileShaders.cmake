function(dtnpc_compile_shaders target shader_src_dir)
  set(shader_build_dir "${CMAKE_CURRENT_BINARY_DIR}/spv")
  set(shader_out_dir "$<TARGET_FILE_DIR:${target}>/shaders")

  file(GLOB_RECURSE shaders CONFIGURE_DEPENDS
    "${shader_src_dir}/*.vert"
    "${shader_src_dir}/*.frag"
    "${shader_src_dir}/*.comp"
  )

  set(spirv_copied_stamps "")
  foreach(shader IN LISTS shaders)
    file(RELATIVE_PATH shader_rel_path "${shader_src_dir}" "${shader}")
    get_filename_component(shader_rel_dir "${shader_rel_path}" DIRECTORY)

    set(spirv "${shader_build_dir}/${shader_rel_path}.spv")
    set(copy_stamp "${CMAKE_CURRENT_BINARY_DIR}/shader_copy_stamps/${shader_rel_path}.stamp")
    set(final_shader_dir "${shader_out_dir}/${shader_rel_dir}")

    add_custom_command(
      OUTPUT "${spirv}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_build_dir}/${shader_rel_dir}"
      COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -o "${spirv}" "${shader}"
      DEPENDS "${shader}"
      COMMENT "Compile shader: ${shader_rel_path}"
      VERBATIM
    )

    add_custom_command(
      OUTPUT "${copy_stamp}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${final_shader_dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${spirv}" "${final_shader_dir}/"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shader_copy_stamps/${shader_rel_dir}"
      COMMAND ${CMAKE_COMMAND} -E touch "${copy_stamp}"
      DEPENDS "${spirv}"
      COMMENT "Copy SPIR-V: ${shader_rel_path} -> ${final_shader_dir}"
      VERBATIM
    )

    list(APPEND spirv_copied_stamps "${copy_stamp}")
  endforeach()

  add_custom_target(${target}_shaders ALL DEPENDS ${spirv_copied_stamps})
  add_dependencies(${target} ${target}_shaders)
endfunction()
