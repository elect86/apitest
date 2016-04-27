#include "pch.h"

#include "appstate.h"
#include "console.h"
#include "factory.h"
#include "gfx.h"
#include "problems/problem.h"
#include "solutions/solution.h"
#include "options.h"
#include "os.h"
#include "timer.h"

#include <stdio.h>

#ifdef _WIN32
#   pragma comment(lib, "imm32.lib")
#   pragma comment(lib, "version.lib")
#   pragma comment(lib, "winmm.lib")
#endif

#ifdef _WIN32
#   define snprintf _snprintf
#endif


std::string asTable(BenchmarkResults _results);
unsigned long long cpuStart, now, cpuTotal = 0, updateCounterStart = 0;
int frames = 0;
GLuint queryName = 0, gpuTotal = 0, gpuTime;

// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
void PostQuitEvent()
{
    SDL_Event quitEvent;
    quitEvent.type = SDL_QUIT;
    SDL_PushEvent(&quitEvent);
}

// --------------------------------------------------------------------------------------------------------------------
void OnEvent(SDL_Event* _event, ApplicationState* _appState)
{
    assert(_event);

    switch (_event->type)
    {
        case SDL_WINDOWEVENT:
        {
            switch (_event->window.event) 
            {
                case SDL_WINDOWEVENT_MOVED:
                {
                    _appState->BroadcastToOtherWindows(_event);
                    break;
                }
                
                // TODO: Need to deal with resizing (to keep windows together, and to resize BB).
                case SDL_WINDOWEVENT_CLOSE:
                {
                    PostQuitEvent();
                    break;

                }

                default:
                {
                    break;
                }
            }
            break;
        }

        case SDL_KEYUP:
        {
            switch (_event->key.keysym.sym)
            {
            case SDLK_F4:
                #ifdef _WIN32
                    if (_event->key.keysym.mod & KMOD_ALT) {
                        PostQuitEvent();
                        break;
                    }
                #endif
                break;

            case SDLK_LEFT:
                if (!_appState->IsBenchmarkMode()) {
                    _appState->PrevProblem();
                }
                break;

            case SDLK_RIGHT:
                if (!_appState->IsBenchmarkMode()) {
                    _appState->NextProblem();
                }
                break;

            case SDLK_UP:
                if (!_appState->IsBenchmarkMode()) {
                    _appState->PrevSolution();
                }
                break;

            case SDLK_DOWN:
                if (!_appState->IsBenchmarkMode()) {
                    _appState->NextSolution();
                }
                break;

            case SDLK_a:
                if (!_appState->IsBenchmarkMode()) {
                    _appState->NextAPI();
                }
                break;

            default:
                break;
            }

            break;
        }
    };
}

// --------------------------------------------------------------------------------------------------------------------
static void Render(Problem* _activeProblem, GfxBaseApi* _activeApi)
{
    if (!_activeProblem) {
        return;
    }

    assert(_activeApi);
    
    Vec4 clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    GLfloat clearDepth = 1.0f;
    _activeProblem->GetClearValues(&clearColor, &clearDepth);

    _activeApi->Clear(clearColor, clearDepth);

	if (queryName == 0) {
		glGenQueries(1, &queryName);
	}
    // This is the main entry point shared by all tests. 
	cpuStart = timer::Read();
	{ 
		glBeginQuery(GL_TIME_ELAPSED, queryName);
		{
			_activeProblem->Render();
			frames++;
		}
		glEndQuery(GL_TIME_ELAPSED);

		// Get the count of samples. 
		// If the result of the query isn't here yet, we wait here...
		glGetQueryObjectuiv(queryName, GL_QUERY_RESULT, &gpuTime);
		gpuTotal += gpuTime;
	}
	cpuTotal += timer::Read() - cpuStart;
	now = timer::Read();
	if (timer::ToSec(now - updateCounterStart) > 1)
	{
		double cpu = timer::ToMSec(cpuTotal) / frames;
		double gpu = double(gpuTotal) / double(1'000'000) / frames;
		double fps = 1'000 / (double(gpuTotal) / 1'000'000 / frames);
		printf("CPU time: %.3f, GPU time: %.3f, theor. FPS: %.3f\n", cpu, gpu, fps);
		frames = 0;
		cpuTotal = 0;
		gpuTotal = 0;
		updateCounterStart = now;
	}
    
    // Present the results.
    _activeApi->SwapBuffers();
}

// --------------------------------------------------------------------------------------------------------------------
static bool InitSDL()
{
    return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) >= 0;
}

// --------------------------------------------------------------------------------------------------------------------
static bool Init(const char* _exeName)
{
    // This forces the working directory to the directory the executable is in. This is necessary
    // to deal with people running from the wrong place (or debuggers).
    std::string dirName = os::path::dirname(_exeName);
    os::chdir(dirName);    

    if (!InitSDL())
    {
        console::error("SDL Error at Initialize: '%s'.\nUnable to initialize SDL -- required -- so exiting.", SDL_GetError());
        return false;
    }

    if (!timer::Init())
    {
        console::error("Unable to initialize timer facilities -- required -- so exiting.");
        return false;
    }

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
static void Cleanup()
{
    SDL_Quit();
}

#include "framework/gfx_gl.h"

// --------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc == 0) {
        console::error("Cannot tell where the executable is, exiting.");
    }

    Options opts = ProcessCommandLine(argc, argv);

    if (!Init(argv[0])) {
        // Technically shouldn't get here--error should exit() if called, and all false cases
        // should emit a message as to why they are exiting. But better safe than sorry.
        return -1;
    }

	updateCounterStart = timer::Read();

    ApplicationState* app = new ApplicationState(opts);

    bool shouldQuit = false;
    for (;;) {
        SDL_Event sdl_event;
        shouldQuit = shouldQuit || (app->IsBenchmarkMode() && app->IsBenchmarkModeComplete());
        if (shouldQuit) {
            break;
        }

        if (SDL_PollEvent(&sdl_event)) {
            shouldQuit = shouldQuit || sdl_event.type == SDL_QUIT;
            OnEvent(&sdl_event, app);
        } else {
			app->Update();
			Render(app->GetActiveProblem(), app->GetActiveApi());
        }
    }

    if (app->IsBenchmarkMode()) {
        console::log("\n\nResults");
        console::log("%s", asTable(app->GetBenchmarkResults()).c_str());
    }

    SafeDelete(app);
    Cleanup();

    return 0;
}

// --------------------------------------------------------------------------------------------------------------------
struct BenchmarkRow
{
    std::string mGfxApiName;
    std::string mProblemName;
    std::string mSolutionName;
    unsigned int mFrameCount;
    double mElapsedS;
    unsigned int mWorkCount;
    double mFramesPerSecond;
    double mMillisecondsPerFrame;
    double mWorkPerSecond;
};

// --------------------------------------------------------------------------------------------------------------------
bool BechmarkSorter(const BenchmarkRow& _lhs, const BenchmarkRow& _rhs)
{
    if (_lhs.mProblemName < _rhs.mProblemName) return true;
    if (_lhs.mProblemName > _rhs.mProblemName) return false;

    if (_lhs.mGfxApiName < _rhs.mGfxApiName) return true;
    if (_lhs.mGfxApiName > _rhs.mGfxApiName) return false;

    return _lhs.mMillisecondsPerFrame < _rhs.mMillisecondsPerFrame;
}

// --------------------------------------------------------------------------------------------------------------------
std::string asTable(BenchmarkResults _results)
{
    char buffer[1024];
    std::string retStr;
    const char* kHeaderFmt =  " %-23s %-10s %-30s %7s %12s %12s %12s\n";
    const char* kRowFmt =     " %-23s %-10s %-30s %7d %12.3f %12.3f %12.3f\n";
    const char* kRowFailFmt = " %-23s %-10s %-30s %7s %12s %12s %12s\n";

    snprintf(buffer, sizeof(buffer)-1, kHeaderFmt, "Problem", "API", "Solution", "Frames", "Elapsed (s)", "fps", "ms/f");
    retStr += buffer;

    std::vector<BenchmarkRow> rows;

    // First, accumulate data into rows.
    for (auto it = _results.cbegin(); it != _results.cend(); ++it) {
        std::string gfxApiName = std::get<0>(it->first);
        std::string problemName = std::get<1>(it->first);
        std::string solutionName = std::get<2>(it->first);
        const unsigned int frameCount = std::get<0>(it->second);
        const double elapsedS = std::get<1>(it->second);
        const unsigned int workCount = std::get<2>(it->second);

        if (frameCount != 0 && elapsedS != 0.0) {
            double fps = frameCount / elapsedS;
            double mspf = elapsedS * 1000.0 / frameCount;
            double wps = workCount / elapsedS;

            BenchmarkRow newRow = {
                gfxApiName, problemName, solutionName,
                frameCount, elapsedS, workCount, fps, mspf, wps
            };

            rows.push_back(newRow);
        } else {
            BenchmarkRow newRow = {
                gfxApiName, problemName, solutionName,
                frameCount, elapsedS, workCount, 0, 0, 0
            };
        }
    }

    std::sort(rows.begin(), rows.end(), BechmarkSorter);

    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        const BenchmarkRow& row = (*it);
    
        if (row.mFrameCount != 0 && row.mElapsedS != 0.0) {
            snprintf(buffer, sizeof(buffer), kRowFmt, row.mProblemName.c_str(), row.mGfxApiName.c_str(), row.mSolutionName.c_str(), row.mFrameCount, row.mElapsedS, row.mFramesPerSecond, row.mMillisecondsPerFrame);
        } else {
            snprintf(buffer, sizeof(buffer), kRowFailFmt, row.mProblemName.c_str(), row.mGfxApiName.c_str(), row.mSolutionName.c_str(), "N/A", "N/A", "N/A", "N/A");
        }
        retStr += buffer;
    }
    return retStr;
}
