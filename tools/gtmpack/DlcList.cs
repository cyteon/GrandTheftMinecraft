// gtmpack dlclist: register dlcpacks:/gtm/ in OpenIV's mods\update\update.rpf (copied from the game's on first use),
// so the game's own update.rpf is never modified. `--remove` takes the entry out again.
using System.Text;
using CodeWalker.GameFiles;

static class DlcList
{
    const string Entry = "dlcpacks:/gtm/";

    public static int Run(string gameDir, bool remove)
    {
        GTA5Keys.LoadFromPath(gameDir);
        string src = Path.Combine(gameDir, "update", "update.rpf");
        string dst = Path.Combine(gameDir, "mods", "update", "update.rpf");
        if (!File.Exists(dst))
        {
            if (remove) { Console.WriteLine(@"no mods\update\update.rpf; nothing to do"); return 0; }
            Directory.CreateDirectory(Path.GetDirectoryName(dst));
            Console.WriteLine($"copying update.rpf to mods ({new FileInfo(src).Length / (1024 * 1024)} MB)...");
            File.Copy(src, dst);
        }
        var rpf = new RpfFile(dst, "update.rpf");
        rpf.ScanStructure(s => { }, e => Console.Error.WriteLine(e));
        var entry = rpf.AllEntries.OfType<RpfFileEntry>().FirstOrDefault(e => e.Path.EndsWith(@"common\data\dlclist.xml", StringComparison.OrdinalIgnoreCase));
        if (entry == null) { Console.Error.WriteLine("dlclist.xml not found in update.rpf"); return 1; }
        string xml = Encoding.UTF8.GetString(entry.File.ExtractFile(entry)).TrimStart('﻿');
        bool has = xml.Contains(Entry, StringComparison.OrdinalIgnoreCase);
        if (has == !remove) { Console.WriteLine(remove ? "entry not present" : "entry already present"); return 0; }
        if (remove)
            xml = string.Join("\n", xml.Split('\n').Where(l => !l.Contains(Entry, StringComparison.OrdinalIgnoreCase)));
        else
        {
            int i = xml.LastIndexOf("</Paths>", StringComparison.OrdinalIgnoreCase);
            if (i < 0) { Console.Error.WriteLine("no </Paths> in dlclist.xml"); return 1; }
            xml = xml.Insert(i, $"\t<Item>{Entry}</Item>\n\t");
        }
        // OpenIV.asi loads OPEN archives; switching the header avoids needing CodeWalker's NG encrypt tables
        if (rpf.Encryption != RpfEncryption.OPEN)
            RpfFile.SetEncryptionType(rpf, RpfEncryption.OPEN);
        RpfFile.CreateFile((RpfDirectoryEntry)entry.Parent, "dlclist.xml", Encoding.UTF8.GetBytes(xml), true);
        Console.WriteLine((remove ? "removed " : "added ") + Entry + " in " + dst);
        return 0;
    }
}
