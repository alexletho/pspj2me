import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;

public class UiTest extends MIDlet implements CommandListener {
    List menu;
    Form form;
    Command exit = new Command("Exit", Command.EXIT, 1);
    Command back = new Command("Back", Command.BACK, 1);
    Command ok = new Command("OK", Command.OK, 1);
    Command help = new Command("Help", Command.HELP, 2);
    TextField name;
    ChoiceGroup cg;
    Gauge gauge;

    protected void startApp() {
        if (menu != null) return;
        menu = new List("Main menu", List.IMPLICIT, new String[] { "New game", "Settings form", "Alert", "About" }, null);
        menu.addCommand(exit);
        menu.setCommandListener(this);
        menu.setTicker(new Ticker("Welcome to the PSPJ2ME UI test"));
        form = new Form("Settings");
        name = new TextField("Name", "Player", 16, TextField.ANY);
        form.append(name);
        cg = new ChoiceGroup("Difficulty", Choice.EXCLUSIVE, new String[] { "Easy", "Normal", "Hard" }, null);
        form.append(cg);
        form.append(new ChoiceGroup("Sound", Choice.MULTIPLE, new String[] { "Music", "Effects" }, null));
        gauge = new Gauge("Volume", true, 10, 7);
        form.append(gauge);
        form.append(new StringItem("Info", "This is a long string item that should wrap over multiple lines nicely."));
        form.append(new DateField("Date", DateField.DATE));
        form.addCommand(back);
        form.addCommand(ok);
        form.addCommand(help);
        form.setCommandListener(this);
        Display.getDisplay(this).setCurrent(menu);
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}

    public void commandAction(Command c, Displayable d) {
        System.out.println("command " + c.getLabel() + " on " + (d == menu ? "menu" : d == form ? "form" : "?"));
        if (c == exit) { notifyDestroyed(); return; }
        if (d == menu && c == List.SELECT_COMMAND) {
            int i = menu.getSelectedIndex();
            System.out.println("selected " + i);
            if (i == 1) Display.getDisplay(this).setCurrent(form);
            if (i == 2) Display.getDisplay(this).setCurrent(new Alert("Hello", "This is an alert!", null, AlertType.INFO), menu);
        }
        if (c == back || c == ok) {
            System.out.println("name=" + name.getString() + " diff=" + cg.getSelectedIndex() + " vol=" + gauge.getValue());
            Display.getDisplay(this).setCurrent(menu);
        }
    }
}
