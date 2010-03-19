INCLUDE(${MYSQL_CMAKE_SCRIPT_DIR}/cmake_parse_arguments.cmake)

MACRO(CREATE_MANIFEST filename EXPORTS NAME)
  FILE(WRITE ${filename} "Manifest-Version: 1.0
Export-Package: ${EXPORTS}
Bundle-Name: ${NAME}
Bundle-Description: ClusterJ")
ENDMACRO(CREATE_MANIFEST)

MACRO(ADD_FILES_TO_JAR TARGET)

  CMAKE_PARSE_ARGUMENTS(ARG
    ""
    ""
    ${ARGN}
  )

  SET(JAVA_CLASSES ${ARG_DEFAULT_ARGS})

  LIST(LENGTH JAVA_CLASSES CLASS_LIST_LENGTH)
  MATH(EXPR EVEN "${CLASS_LIST_LENGTH}%2")
  IF(EVEN GREATER 0)
    MESSAGE(SEND_ERROR "CREATE_JAR_FROM_CLASSES has ${CLASS_LIST_LENGTH} but needs equal number of class parameters")
  ENDIF()

  MATH(EXPR CLASS_LIST_LENGTH "${CLASS_LIST_LENGTH} - 2")


  FOREACH(I RANGE 0 ${CLASS_LIST_LENGTH} 2)

    MATH(EXPR J "${I} + 1")
    LIST(GET JAVA_CLASSES ${I} DIR)
    LIST(GET JAVA_CLASSES ${J} IT)
    SET(CLASS_DIRS -C ${DIR} ${IT})

    ADD_CUSTOM_COMMAND( TARGET  ${TARGET}.jar POST_BUILD
      COMMAND echo "${JAVA_ARCHIVE} ufv ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar ${CLASS_DIRS}"
      COMMAND ${JAVA_ARCHIVE} ufv ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar ${CLASS_DIRS}
      COMMENT "adding ${CLASS_DIRS} to target ${TARGET}-${JAVA_NDB_VERSION}.jar")

  ENDFOREACH(I RANGE 0 ${CLASS_LIST_LENGTH} 2)

ENDMACRO(ADD_FILES_TO_JAR)


MACRO(CREATE_JAR_FROM_CLASSES TARGET)

  CMAKE_PARSE_ARGUMENTS(ARG
    "DEPENDENCIES;MANIFEST"
    ""
    ${ARGN}
  )

  ADD_CUSTOM_TARGET( ${TARGET}.jar ALL
      COMMAND echo "${JAVA_ARCHIVE} cfvm ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar ${ARG_MANIFEST}"
      COMMAND ${JAVA_ARCHIVE} cfvm ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar ${ARG_MANIFEST} )
  FOREACH(DEP ${ARG_DEPENDENCIES})
    ADD_DEPENDENCIES(${TARGET}.jar ${DEP})
  ENDFOREACH(DEP ${ARG_DEPENDENCIES})

  ADD_FILES_TO_JAR(${TARGET} "${ARG_DEFAULT_ARGS}")

ENDMACRO(CREATE_JAR_FROM_CLASSES)


# the target
MACRO(CREATE_JAR)

  CMAKE_PARSE_ARGUMENTS(ARG
    "CLASSPATH;DEPENDENCIES;MANIFEST;ENHANCE;EXTRA_FILES"
    ""
    ${ARGN}
  )

  LIST(GET ARG_DEFAULT_ARGS 0 TARGET)
  SET(JAVA_FILES ${ARG_DEFAULT_ARGS})
  LIST(REMOVE_AT JAVA_FILES 0)

  SET (CLASS_DIR "target/classes")
  SET (JAR_DIR ".")

  FILE(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CLASS_DIR})
  FILE(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${JAR_DIR})
  SET(TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/${CLASS_DIR})

  ADD_CUSTOM_TARGET( ${TARGET}.jar ALL
    COMMAND echo "${JAVA_ARCHIVE} cfv ${JAR_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar -C ${CLASS_DIR} ."
    COMMAND ${JAVA_ARCHIVE} cfv ${JAR_DIR}/${TARGET}-${JAVA_NDB_VERSION}.jar -C ${CLASS_DIR} .)

  IF(EXISTS ${ARG_ENHANCE})
    MESSAGE(STATUS "enhancing ${TARGET}.jar")
    SET(ENHANCER org.apache.openjpa.enhance.PCEnhancer)
    ADD_CUSTOM_COMMAND( TARGET ${TARGET}.jar PRE_BUILD
      COMMAND echo "${JAVA_COMPILE} -d ${TARGET_DIR} -classpath ${ARG_CLASSPATH} ${JAVA_FILES}"
      COMMAND ${JAVA_COMPILE} -d ${TARGET_DIR} -classpath "${ARG_CLASSPATH}" ${JAVA_FILES}
      COMMAND echo "${JAVA_RUNTIME} -classpath ${ARG_CLASSPATH}:${WITH_CLASSPATH} ${ENHANCER} -p ${ARG_ENHANCE} -d ${TARGET_DIR}"
      COMMAND ${JAVA_RUNTIME} -classpath "${ARG_CLASSPATH};${WITH_CLASSPATH}" ${ENHANCER} -p ${ARG_ENHANCE} -d ${TARGET_DIR}
    )
  ELSE()
    ADD_CUSTOM_COMMAND( TARGET ${TARGET}.jar PRE_BUILD
      COMMAND echo "${JAVA_COMPILE} -d ${TARGET_DIR} -classpath ${ARG_CLASSPATH} ${JAVA_FILES}"
      COMMAND ${JAVA_COMPILE} -d ${TARGET_DIR} -classpath "${ARG_CLASSPATH}" ${JAVA_FILES}
    )
  ENDIF()

  LIST(LENGTH ARG_EXTRA_FILES LIST_LENGTH)
  IF(LIST_LENGTH GREATER 0)
    ADD_FILES_TO_JAR(${TARGET} "${ARG_EXTRA_FILES}")
  ENDIF()

  FOREACH(DEP ${ARG_DEPENDENCIES})
    ADD_DEPENDENCIES(${TARGET}.jar ${DEP})
  ENDFOREACH(DEP ${ARG_DEPENDENCIES})

ENDMACRO()
