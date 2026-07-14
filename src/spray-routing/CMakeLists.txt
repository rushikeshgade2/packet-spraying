build_lib(
    LIBNAME spray-routing
    SOURCE_FILES
        model/spray-routing.cc
        model/elephant-tag.cc
        helper/spray-routing-helper.cc
    HEADER_FILES
        model/spray-routing.h
        model/elephant-tag.h
        helper/spray-routing-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libpoint-to-point}
        ${libapplications}
        ${libflow-monitor}
)
