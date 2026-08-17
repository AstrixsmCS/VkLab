#include "Application.hpp"

int main(int argc, char *argv[])
{
	Application app;
	if (app.Initialize())
	{
		app.Run();
	}
	app.Shutdown();

	return 0;
}
