project "test"
	kind "ConsoleApp"
	language "C++"
	targetdir "bin/%{cfg.buildcfg}"

	dependson { "libgilberta" }
	links { "libgilberta" }

	includedirs { "include" }

	files { "src/test/**.cpp", "src/test/**.c", "src/test/**.h" }

	filter "configurations:Debug"
		defines { "DEBUG" }
		optimize "Off"
		symbols "On"
		runtime "Debug"
		staticruntime "Off"

	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "On"
		symbols "On"
		runtime "Release"
		staticruntime "Off"