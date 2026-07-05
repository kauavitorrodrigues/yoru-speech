# Cross-cutting infrastructure shared by every domain.
add_subdirectory(core)

# Business domains. Each subdirectory registers its own targets as it gains
# an implementation along the roadmap.
# add_subdirectory(domains/...)

# Composition root: wires the domains together into the service executable.
add_subdirectory(app)
