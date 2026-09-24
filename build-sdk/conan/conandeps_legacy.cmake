message(STATUS "Conan: Using CMakeDeps conandeps_legacy.cmake aggregator via include()")
message(STATUS "Conan: It is recommended to use explicit find_package() per dependency instead")

find_package(gRPC)
find_package(Eigen3)
find_package(tinyxml2)
find_package(nlohmann_json)

set(CONANDEPS_LEGACY  grpc::grpc  Eigen3::Eigen  tinyxml2::tinyxml2  nlohmann_json::nlohmann_json )