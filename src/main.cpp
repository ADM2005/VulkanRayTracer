#include "include/ray_tracer.hpp"

int main() {
	RayTracer rayTracer{};

	rayTracer.init();

	rayTracer.run();

	rayTracer.cleanup();

	return 0;

}