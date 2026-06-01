#include "lgi/common/Lgi.h"
#include "lgi/common/TextLog.h"
#include "lgi/common/TabView.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/CommsBus.h"

#include "ufwGui/ufwGui.h"

// sudo netstat -tulnp | grep 45454

//////////////////////////////////////////////////////////////////
const char *AppName = "ufwGui";

enum Ctrls {
    ID_LOG = 100,
    ID_TABS,
    ID_COMMS_LOG,
    ID_COMMS_STATE,
};

class UfwGuiApp : public LWindow
{
    LTextLog *txtLog = nullptr;
    LTextLog *commsLog = nullptr;
    LTextLog *commsState = nullptr;
    LTabView *tabs = nullptr;
    LAutoPtr<LSubProcess> worker;
    LAutoPtr<LCommsBus> bus;

    enum TState {
        TInit,
        TGetStatus,
        THasStatus,
        TQuit,
    }   state = TInit;

public:
    UfwGuiApp()
    {
        Name(AppName);
        LRect r(0, 0, 1000, 800);
        SetPos(r);
        MoveToCenter();
        SetQuitOnClose(true);

        if (!Attach(nullptr))
            return;

        if (tabs = new LTabView(ID_TABS))
            AddView(tabs);
        else return;

        auto tab = tabs->Append("Log");
        if (txtLog = new LTextLog(ID_LOG))
            tab->AddView(txtLog);

        tab = tabs->Append("Comms");
        if (commsLog = new LTextLog(ID_COMMS_LOG))
            tab->AddView(commsLog);

        tab = tabs->Append("State");
        if (commsState = new LTextLog(ID_COMMS_STATE))
            tab->AddView(commsState);

        if (bus.Reset(new LCommsBus(commsLog, commsState)))
        {
            bus->Listen(EP_SCAN_RESULT,
                [this](auto str)
                {
                    ScanResult(str);
                });
        }

        AttachChildren();
        Visible(true);

        LFile::Path p(LSP_APP_INSTALL);
        auto workerPath = p / "ufwWorker";
        if (workerPath.Exists())
        {
            if (worker.Reset(new LSubProcess("pkexec", workerPath)))
            {
                if (worker->Start())
                    txtLog->Print("%s:%i - started worker...\n", _FL);
                else
                    txtLog->Print("%s:%i - error: failed to start worker...\n", _FL);
            }
        }
        else txtLog->Print("%s:%i - failed to find worker: '%s'\n", _FL, workerPath.GetFull().Get());

        SetPulse(1000);
    }

    ~UfwGuiApp()
    {        
    }

    void ScanResult(LString json)
    {
        state = THasStatus;
        txtLog->Print("Got result: %s\n", json.Get());
    }

    void OnPulse()
    {
        switch (state)
        {
            case TInit:
            {
                state = TGetStatus;
                break;
            }
            case TGetStatus:
            {
                txtLog->Print("%s:%i - requesting rules...\n", _FL);
                bus->SendMsg(EP_SCAN_RULES, LString());
                break;
            }
            case TQuit:
            {
                if (worker)
                {
                    if (worker->IsRunning())
                    {
                        txtLog->Print("%s:%i - sending quit...\n", _FL);
                        bus->SendMsg(EP_QUIT, LString());
                    }
                    else
                    {
                        txtLog->Print("%s:%i - worker has exited...\n", _FL);
                        worker.Reset();
                        LCloseApp();
                    }
                }
                break;
            }
        }
    }

    bool OnRequestClose(bool OsShuttingDown) override
    {
        if (worker)
        {
            state = TQuit;
            return false;
        }

        return true;
    }
};

//////////////////////////////////////////////////////////////////
int LgiMain(OsAppArguments &AppArgs)
{
	LApp a(AppArgs, AppName);
	if (a.IsOk())
	{
		a.AppWnd = new UfwGuiApp;
		a.Run();
	}

	return 0;
}

