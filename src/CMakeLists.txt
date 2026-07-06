# Business domains. Each subdirectory registers its own targets as it gains
# an implementation along the roadmap.
add_subdirectory(domains/session)
add_subdirectory(domains/speech)
add_subdirectory(domains/config)
add_subdirectory(domains/audio)

# Cross-cutting infrastructure shared by every domain. Depends on nothing;
# every domain may depend on it, never the other way around.
add_subdirectory(core)

# Composition root: wires the domains together into the service executable.
add_subdirectory(app)
