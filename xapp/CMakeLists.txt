add_executable(xapp_kpm_moni 
		xapp_kpm_moni.c
                ../../../../src/util/alg_ds/alg/defer.c
              )

set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-1.23.2")

target_include_directories(xapp_kpm_moni
                           PRIVATE
                           "${ONNXRUNTIME_ROOT}/include"
                           )

target_link_libraries(xapp_kpm_moni
                      PUBLIC
                      e42_xapp
                      -pthread
                      -lsctp
                      -ldl
                      -lcrypto
                      "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so"
                      )

set_target_properties(xapp_kpm_moni
                      PROPERTIES
                      BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
                      )

add_executable(xapp_gtp_mac_rlc_pdcp_moni 
		xapp_gtp_mac_rlc_pdcp_moni.c
                ../../../../src/util/alg_ds/alg/defer.c
              )

 target_link_libraries(xapp_gtp_mac_rlc_pdcp_moni
                      PUBLIC
                      e42_xapp
                      -pthread
                      -lsctp
                      -ldl
                      )

add_executable(xapp_rc_moni 
               xapp_rc_moni.c
               ../../../../src/util/alg_ds/alg/defer.c
              )
                  
target_link_libraries(xapp_rc_moni
                     PUBLIC
                     e42_xapp
                     -pthread
                     -lsctp
                     -ldl
                     )
