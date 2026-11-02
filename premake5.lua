workspace "gilberta"
	configurations { "Debug", "Release" }
	architecture "x86_64"
	location "./"

	VCPKG_ROOT = os.getenv("VCPKG_ROOT") or "C:/vcpkg"
	VCPKG_TRIPLET = "x64-windows"

	includedirs {
		VCPKG_ROOT .. "/installed/" .. VCPKG_TRIPLET .. "/include"
	}
	libdirs {
		VCPKG_ROOT .. "/installed/" .. VCPKG_TRIPLET .. "/lib"
	}

include "libgilberta"
include "test"