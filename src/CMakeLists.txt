# Cross-cutting infrastructure shared by every domain.
add_subdirectory(core)

# Business domains. Each subdirectory registers its own targets as it gains
# an implementation along the roadmap.
add_subdirectory(domains/session)
add_subdirectory(domains/speech)
add_subdirectory(domains/config)

# Composition root: wires the domains together into the service executable.
add_subdirectory(app)
