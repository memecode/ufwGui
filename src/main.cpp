#include "lgi/common/File.h"
#include "lgi/common/Lgi.h"
#include "lgi/common/LgiDefs.h"
#include "lgi/common/LgiInterfaces.h"
#include "lgi/common/Net.h"
#include "lgi/common/Notifications.h"
#include "lgi/common/Stream.h"
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
#include "lgi/common/List.h"
#include "lgi/common/ListItemCheckBox.h"
#include "lgi/common/Button.h"

#include "ufwGui/ufwGui.h"
#include <functional>
#include <utility>

// sudo netstat -tulnp | grep 45454

//////////////////////////////////////////////////////////////////
const char *AppName = "ufwGui";

enum Ctrls {
    ID_LOG = 100,   // LTextLog
    ID_TABS,        // LTabView
    ID_TABLE,       // LTableLayout
    ID_COMMS_LOG,   // LTextLog
    ID_COMMS_STATE, // LTextLog
    ID_UFW_ENABLE,  // LCheckBox
    ID_RULES,       // LList
    ID_ADD,         // LButton
    ID_DELETE,      // LButton
};

enum TCol {
    ColSel,
    ColNumber,
    ColText,
};

class RuleItem : public LListItem
{
    LListItemCheckBox *chk = nullptr;

public:
    int64_t index = -1;

    RuleItem(LString::Array &parts)
    {
        chk = new LListItemCheckBox(this, ColSel);

        if (parts.Length() == 2)
        {
            auto s = parts[0].Strip(); 
            index = s.Int();
            SetText(s, ColNumber);
            SetText(parts[1].Strip(), ColText);
        }
        else
        {
            for (int i=0; i<parts.Length(); i++)
                printf("part[%i]='%s'\n", i, parts[i].Get());
        }
    }

    bool operator ==(const RuleItem &r) const
    {
        return index == r.index;
    }

    bool IsChecked() const
    {
        return chk ? chk->Value() : false;
    }
};

class UfwGuiApp : public LWindow
{
    LTableLayout *tbl = nullptr;
    LCheckBox *chkEnable = nullptr;
    LTextLog *txtLog = nullptr;
    LTextLog *commsLog = nullptr;
    LTextLog *commsState = nullptr;
    LList *lstRules = nullptr;
    LTabView *tabs = nullptr;
    LAutoPtr<LSubProcess> worker;
    LAutoPtr<LCommsBus> bus;
    LFile commsStateLog;
    LAutoPtr<LStreamTee> tee;

    enum TState
    {
        TInit,
        TRunning,
        TQuit,
    }   state = TInit;

    enum TStatus {
        ESuccess = 0,
        EError   = -1,
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
    LArray<Cmd*> cmds;
    int nextRef = 10;

    void Run(LString args, TCallback cb)
    {
        if (auto cmd = new Cmd)
        {
            cmd->args = args;
            cmd->cb = std::move(cb);
            cmd->ref = nextRef++;
            cmd->startTs = LCurrentTime();
            cmds.Add(cmd);

            LJson j;
            j.Set("ref", (int64_t)cmd->ref);
            j.Set("args", args);
            bus->SendMsg(EP_UFW_RUN, j.GetJson());
        }
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
        int row = 0;
        int cols = 3;
        auto c = tbl->GetCell(0, row);
        c->Add(new LTextLabel(ID_STATIC, 0, 0, -1, -1, "Enable ufw:"));
        c = tbl->GetCell(1, row);
        if (c->Add(chkEnable = new LCheckBox(ID_UFW_ENABLE, "", false)))
            chkEnable->Enabled(false);

        // List of rules row:
        c = tbl->GetCell(0, ++row, true, 2);
        if (c->Add(lstRules = new LList(ID_RULES)))
        {
            lstRules->AddColumn("Sel");
            lstRules->AddColumn("Number");
            lstRules->AddColumn("Text");
        }
        c = tbl->GetCell(2, row);
        c->Add(new LButton(ID_ADD, 0, 0, -1, -1, "Add"));
        c->Add(new LButton(ID_DELETE, 0, 0, -1, -1, "Del"));

        // Log row:
        c = tbl->GetCell(0, ++row, true, cols);
        if (txtLog = new LTextLog(ID_LOG))
            c->Add(txtLog);
        else
            return;

        tab = tabs->Append("Comms");
        commsStateLog.Open(LFile::Path(LSP_APP_INSTALL) / "commsState.log", O_WRITE);
        commsStateLog.SetSize(0);
        if (commsLog = new LTextLog(ID_COMMS_LOG))
            tab->AddView(commsLog);
        else
            return;
            
        tee.Reset(new LStreamTee(commsLog, &commsStateLog));
        LSetNetworkLog(tee.Get());

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

    int OnNotify(LViewI *c, const LNotification &n) override
    {
        switch (c->GetId())
        {
            case ID_UFW_ENABLE:
            {
                if (n.Type == LNotifyValueChanged)
                {
                    c->Enabled(false);
                    Run(c->Value() ? "enable" : "disable",
                        [this, c, enable = c->Value()](auto code, auto str, auto &json)
                        {
                            c->Enabled(true);
                            if (code == 0)
                            {
                                c->Value(enable);

                                // Update the status:
                                UfwStatus();
                            };
                        });
                }
                else
                    txtLog->Print("%s:%i - unexpected notify: %i\n", _FL, n.Type);
                break;
            }
            case ID_DELETE:
            {
                LArray<RuleItem*> rules;
                if (!lstRules->GetAll(rules))
                    break;

                LArray<RuleItem*> del;
                for (auto r: rules)
                    if (r->IsChecked())
                        del.Add(r);

                // sort highest to lowest......
                del.Sort([](auto *a, auto *b)
                    {
                        return (int) ((*b)->index - (*a)->index);
                    });

                for (auto r: del)
                {
                    txtLog->Print("del %i\n", (int)r->index);
                    Run(LString::Fmt("delete " LPrintfInt64, r->index),
                        [this, idx = r->index](auto code, auto str, auto &json)
                        {
                            if (code)
                            {
                                txtLog->Print("delete err: %i, %s\n", (int)code, str.Get());
                                return;
                            }
                            
                            LArray<RuleItem*> rules;
                            if (!lstRules->GetAll(rules))
                                return;
                            for (auto r: rules)
                                if (r->index == idx)
                                {
                                    delete r;
                                    break;
                                }
                        });
                }
                break;
            }
        }

        return 0;
    }

    void UfwResult(LString json)
    {
        LJson j(json);

        // txtLog->Print("Got result: %s\n", json.Get());

        auto ref = j.Get("ref");
        for (size_t i=0; i<cmds.Length(); i++)
        {
            auto *cmd = cmds[i];
            if (cmd->ref == ref.Int())
            {
                auto exit = j.Get("exit");
                auto stdout = j.Get("stdout");
                if (cmd->cb)
                    cmd->cb(exit.Int(), stdout, j);

                // remove the cmd from the array...
                cmds.DeleteAt(i);
                delete cmd;
                return;
            }
        }

        txtLog->Print("%s:%i error: no handler for ref '%s'\n", _FL, ref.Get());
    }

    void OnActive(bool active)
    {
        if (chkEnable)
            chkEnable->Value(active);
    }

    bool HasRule(RuleItem *rule)
    {
        LArray<RuleItem*> rules;
        if (!lstRules->GetAll(rules))
            return false;
        
        for (auto r: rules)
            if (*r == *rule)
                return true;

        return false;
    }

    void UfwStatus()
    {
        // txtLog->Print("running status...\n");
        Run("status numbered",
            [this](auto exitCode, auto str, auto &json)
            {
                if (exitCode == 0)
                {
                    state = TRunning;

                    auto lines = str.SplitDelimit("\n");
                    for (auto &ln: lines)
                    {
                        if (ln.Find("Status:") == 0)
                        {
                            // status line:
                            auto status = ln.SplitDelimit(": ");
                            auto active = status[-1].Equals("active");
                            OnActive(active);
                        }
                        else if (ln(0) == '[')
                        {
                            // rule line:
                            auto parts = ln.SplitDelimit("[]");
                            txtLog->Print("rule: %s\n", ln.Get());

                            auto item = new RuleItem(parts);
                            if (lstRules && !HasRule(item))
                                lstRules->Insert(item);
                            else
                                delete item;
                        }
                        else
                        {
                            // unhandled line:
                            txtLog->Print("unhandled: %s\n", ln.Get());
                        }
                    }

                    if (chkEnable)
                        chkEnable->Enabled(true);
                    if (lstRules)
                        lstRules->ResizeColumnsToContent();
                }
                else
                {
                    txtLog->Print("Status err: %i, %s, %s\n", (int)exitCode, str.Get(), json.Get("args").Get());
                }
            });
    }

    void OnPulse()
    {
        // Handle timed out commands...
        auto now = LCurrentTime();
        for (size_t i=0; i<cmds.Length(); i++)
        {
            auto cmd = cmds[i];
            if (now - cmd->startTs >= TIMEOUT_MS)
            {
                if (cmd->cb)
                {
                    LJson j;
                    cmd->cb(ETimeout, LString(), j);
                }
                cmds.DeleteAt(i--);
                delete cmd;
                txtLog->Print("%s:%i - deleted timed out, %i remain\n", _FL, (int)cmds.Length());
            }
        }

        switch (state)
        {
            case TInit:
            {
                if (!cmds.Length())
                    UfwStatus();
                // else txtLog->Print("%s:%i - %i cmds exist...\n", _FL, (int)cmds.Length());
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

