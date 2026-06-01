#include "lgi/common/Lgi.h"
#include "lgi/common/CommsBus.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/Json.h"
#include "lgi/common/Net.h"

#include "ufwGui/ufwGui.h"

//////////////////////////////////////////////////////////////////
const char *AppName = "ufwWorker";

struct LLogFile : public LStream
{
    LAutoPtr<LFile> f;

    LLogFile()
    {
        LFile::Path p(LSP_APP_INSTALL);
        if (f.Reset(new LFile(p / "worker.log", O_WRITE)))
            f->SetSize(0);
    }

	ssize_t Write(const void *Ptr, ssize_t Size, int Flags = 0) override
    {
        if (f)
            f->Write(Ptr, Size);
        return Size;
    }

}   logger;

class App : public LCancel
{
    LCommsBus bus;

public:
    App() : bus(&logger)
    {
        bus.Listen(EP_SCAN_RULES,
            [this](auto str)
            {
                logger.Print("%s:%i - got scan req..\n", _FL);
                ScanRules(str);
            });

        bus.Listen(EP_QUIT,
            [this](auto str)
            {
                logger.Print("%s:%i - got quit req..\n", _FL);
                Cancel();
            });
    }

    void ScanRules(LString args)
    {
        LSubProcess ufw("ufw", "status");
        LJson out;

        logger.Print("%s:%i - starting ufw..\n", _FL);
        if (ufw.Start())
        {
            LStringPipe p;
            logger.Print("%s:%i - comms ufw..\n", _FL);
            ufw.Communicate(&p, nullptr, this);
            out.Set("stdout", p.NewLStr());
            out.Set("exit", (int64_t)ufw.GetExitValue());
        }
        else out.Set("exit", (int64_t)-1);

        logger.Print("%s:%i - sending output..\n", _FL);
        bus.SendMsg(EP_SCAN_RESULT, out.GetJson());
    }

    void Run()
    {
        while (!IsCancelled())
        {
            LSleep(10);
        }
    }
};

//////////////////////////////////////////////////////////////////
int LgiMain(OsAppArguments &AppArgs)
{
	LApp a(AppArgs, AppName);
	if (a.IsOk())
	{
        LSetNetworkLog(&logger);

        App a;
        a.Run();
	}

	return 0;
}

