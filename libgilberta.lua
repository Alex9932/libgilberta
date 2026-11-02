project "libgilberta"
	kind "SharedLib"
	language "C"
	targetdir "bin/%{cfg.buildcfg}"
	links {  }

	includedirs { "include" }
	
	files { "src/libgilberta/**.c", "src/libgilberta/**.h", "include/**.h" }

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