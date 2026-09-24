# CMake generated Testfile for 
# Source directory: C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern
# Build directory: C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/build-sdk
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[RBY1SdkTests]=] "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/build-sdk/RBY1SdkTests.exe" "-o" "-,txt" "-o" "sdk-results.xml,junitxml")
set_tests_properties([=[RBY1SdkTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;121;add_test;C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;0;")
add_test([=[RBY1PlanningTests]=] "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/build-sdk/RBY1PlanningTests.exe" "-o" "-,txt" "-o" "planning-results.xml,junitxml")
set_tests_properties([=[RBY1PlanningTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;131;add_test;C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;0;")
add_test([=[PlanningMockTests]=] "C:/Python314/python.exe" "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/tests/test_mock_planning.py")
set_tests_properties([=[PlanningMockTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;134;add_test;C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/CMakeLists.txt;0;")
subdirs("rby1-sdk")
