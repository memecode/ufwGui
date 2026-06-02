#include "lgi/common/Lgi.h"
#include "lgi/common/LgiInterfaces.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/TextLog.h"
#include "lgi/common/TabView.h"
#include "lgi/common/SubProcess.h"
#include "lgi/common/CommsBus.h"
#include "lgi/common/Json.h"
#include "lgi/common/TableLayout.h"
#include "lgi/common/Box.h"
#include "lgi/common/TextLabel.h"
#include "lgi/common/CheckBox.h"

#include "ufwGui/ufwGui.h"
#include <functional>
#include <utility>

// sudo netstat -tulnp | grep 45454

//////////////////////////////////////////////////////////////////
const char *AppName = "ufwGui";

enum Ctrls {
    ID_LOG = 100,
    ID_TABS,
    ID_COMMS_LOG,
    ID_COMMS_STATE,
    ID_TABLE,
    ID_UFW_ENABLE,
};

class UfwGuiApp : public LWindow
{
    LTableLayout *tbl = nullptr;
    LCheckBox *chkEnable = nullptr;
    LTextLog *txtLog = nullptr;
    LTextLog *commsLog = nullptr;
    LTextLog *commsState = nullptr;
    LTabView *tabs = nullptr;
    LAutoPtr<LSubProcess> worker;
    LAutoPtr<LCommsBus> bus;

    enum TState
    {
        TInit,
        TRunning,
        TQuit,
    }   state = TInit;

    enum TStatus {
        ESuccess = 0,
        EError = -1,
        ETimeout = -2,
    };

    constexpr static int TIMEOUT_MS = 5000;
    using TCallback = std::function<void(int64_t,LString,LJson&)>;
    struct Cmd
    {
        int ref;
        uint64_t startTs;
        LString args;
        TCallback cb;
    };
    LArray<Cmd> cmds;
    int nextRef = 10;

    void Run(LString args, TCallback cb)
    {
        auto &cmd = cmds.New();
        cmd.args = args;
        cmd.cb = std::move(cb);
        cmd.ref = nextRef++;
        cmd.startTs = LCurrentTime();

        LJson j;
        j.Set("ref", (int64_t)cmd.ref);
        j.Set("args", args);
        bus->SendMsg(EP_UFW_RUN, j.GetJson());
    }

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

        auto tab = tabs->Append("Manage");
        if (tbl = new LTableLayout(ID_TABLE))
            tab->Append(tbl);
        else
            return;
        auto c = tbl->GetCell(0, 0);
        c->Add(new LTextLabel(ID_STATIC, 0, 0, -1, -1, "Enable ufw:"));
        c = tbl->GetCell(1, 0);
        c->Add(chkEnable = new LCheckBox(ID_UFW_ENABLE, "", false));
        c = tbl->GetCell(0, 1, true, 2);
        if (txtLog = new LTextLog(ID_LOG))
            c->Add(txtLog);

        tab = tabs->Append("Comms");
        if (commsLog = new LTextLog(ID_COMMS_LOG))
            tab->AddView(commsLog);

        tab = tabs->Append("State");
        if (commsState = new LTextLog(ID_COMMS_STATE))
            tab->AddView(commsState);

        if (bus.Reset(new LCommsBus(commsLog, commsState)))
        {
            // Handle processing of results coming back from the worker:
            bus->Listen(EP_UFW_RESULT,
                [this](auto str)
                {
                    RunCallback([this, str]()
                        {
                            UfwResult(str);
                        },
                        _FL);
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

    void UfwResult(LString json)
    {
        LJson j(json);

        txtLog->Print("Got result: %s\n", json.Get());

        auto ref = j.Get("ref");
        for (size_t i=0; i<cmds.Length(); i++)
        {
            if (cmds[i].ref == ref.Int())
            {
                auto exit = j.Get("exit");
                auto stdout = j.Get("stdout");
                if (cmds[i].cb)
                    cmds[i].cb(exit.Int(), stdout, j);

                // remove the cmd from the array...
                cmds.DeleteAt(i);
                return;
            }
        }

        txtLog->Print("%s:%i error: no handler for ref '%s'\n", _FL, ref.Get());
    }

    void OnPulse()
    {
        // Handle timed out commands...
        auto now = LCurrentTime();
        for (size_t i=0; i<cmds.Length(); i++)
        {
            auto &cmd = cmds[i];
            if (now - cmd.startTs >= TIMEOUT_MS)
            {
                if (cmd.cb)
                {
                    LJson j;
                    cmd.cb(ETimeout, LString(), j);
                }
                cmds.DeleteAt(i--);
                txtLog->Print("%s:%i - deleted timed out, %i remain\n", _FL, (int)cmds.Length());
            }
        }

        switch (state)
        {
            case TInit:
            {
                if (!cmds.Length())
                {
                    txtLog->Print("running status...\n");
                    Run("status", [this](auto exitCode, auto str, auto &json)
                        {
                            if (exitCode == 0)
                                state = TRunning;
                            txtLog->Print("status: %i, %s, %s\n", (int)exitCode, str.Get(), json.Get("args").Get());
                        });
                }
                else txtLog->Print("%s:%i - %i cmds exist...\n", _FL, (int)cmds.Length());
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

