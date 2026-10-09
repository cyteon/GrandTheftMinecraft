// gtmpack dlclist: register dlcpacks:/gtm/ in OpenIV's mods\update\update.rpf (copied from the game's on first use),
// so the game's own update.rpf is never modified. `--remove` takes the entry out again.
using System.Text;
using CodeWalker.GameFiles;

static class DlcList
{
    const string Entry = "dlcpacks:/gtm/";
    // GTA keeps one fwDynamicArchetypeComponent per prop type in play; the block pack's thousands of models plus a
    // busy city ran the stock 16384 dry (a crash in GTA's pool allocator). A bigger pool costs ~400 KB.
    const string Pool = "fwDynamicArchetypeComponent";
    const int PoolStock = 16384, PoolOurs = 24576;

    // the PoolSize value right after <PoolName>Pool</PoolName>; returns the new xml (or null if not found)
    static string SetPool(string xml, int from, int to)
    {
        int n = xml.IndexOf($"<PoolName>{Pool}</PoolName>", StringComparison.Ordinal);
        if (n < 0) return null;
        int v = xml.IndexOf("<PoolSize value=\"", n, StringComparison.Ordinal);
        if (v < 0) return null;
        v += "<PoolSize value=\"".Length;
        int e = xml.IndexOf('"', v);
        if (!int.TryParse(xml.Substring(v, e - v), out int cur)) return null;
        if (to > from ? cur >= to : cur != from) return xml; // already big enough / not ours to shrink
        return xml.Substring(0, v) + to + xml.Substring(e);
    }

    static void PatchGameConfig(RpfFile rpf, bool remove)
    {
        var entry = rpf.AllEntries.OfType<RpfFileEntry>().FirstOrDefault(e => e.Path.EndsWith(@"common\data\gameconfig.xml", StringComparison.OrdinalIgnoreCase));
        if (entry == null) { Console.Error.WriteLine("gameconfig.xml not found in update.rpf (pool left as is)"); return; }
        string xml = Encoding.UTF8.GetString(entry.File.ExtractFile(entry)).TrimStart('\uFEFF');
        string patched = remove ? SetPool(xml, PoolOurs, PoolStock) : SetPool(xml, PoolStock, PoolOurs);
        if (patched == null) { Console.Error.WriteLine($"{Pool} not found in gameconfig.xml (pool left as is)"); return; }
        if (patched == xml) { Console.WriteLine($"{Pool}: nothing to change"); return; }
        RpfFile.CreateFile((RpfDirectoryEntry)entry.Parent, "gameconfig.xml", Encoding.UTF8.GetBytes(patched), true);
        Console.WriteLine($"{Pool} pool set to {(remove ? PoolStock : PoolOurs)}");
    }

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
        if (rpf.Encryption != RpfEncryption.OPEN)
            RpfFile.SetEncryptionType(rpf, RpfEncryption.OPEN);
        PatchGameConfig(rpf, remove);
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
