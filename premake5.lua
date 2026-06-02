workspace "netlib"
	configurations { "Debug", "Release" }
	architecture "x86_64"
	location "build"

	VCPKG_ROOT = os.getenv("VCPKG_ROOT") or "C:/vcpkg"
	VCPKG_TRIPLET = "x64-windows"

	includedirs {
		VCPKG_ROOT .. "/installed/" .. VCPKG_TRIPLET .. "/include"
	}
	libdirs {
		VCPKG_ROOT .. "/installed/" .. VCPKG_TRIPLET .. "/lib"
	}

project "libgilberta"
	kind "SharedLib"
	language "C++"
	targetdir "bin/%{cfg.buildcfg}"
	links {  }

	files { "src/libgilberta/**.cpp", "src/libgilberta/**.c", "src/libgilberta/**.h" }

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "NDEBUG" }
		symbols "On"

project "test"
	kind "ConsoleApp"
	language "C++"
	targetdir "bin/%{cfg.buildcfg}"

	dependson { "libgilberta" }
	links { "libgilberta" }

	includedirs { "src/libgilberta" }

	files { "src/test/**.cpp", "src/test/**.c", "src/test/**.h" }

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "NDEBUG" }
		symbols "On"