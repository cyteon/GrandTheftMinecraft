// gtmpack: builds the GrandTheftMinecraft add-on DLC (textured 1 m block props) with CodeWalker.Core.
//   gtmpack export <gamedir> <file.ydr|ytd|ytyp> <outdir>   dump a vanilla file to CodeWalker XML (template study)
using CodeWalker.GameFiles;

static class Program
{
    static RpfManager OpenGame(string gameDir)
    {
        GTA5Keys.LoadFromPath(gameDir);
        var rpf = new RpfManager();
        rpf.Init(gameDir, false, s => { }, e => Console.Error.WriteLine(e), false, true);
        Console.WriteLine($"keys: aes={(GTA5Keys.PC_AES_KEY != null)} ng={(GTA5Keys.PC_NG_KEYS != null)}; rpfs {rpf.AllRpfs.Count}, entries {rpf.EntryDict.Count}");
        return rpf;
    }

    static int Export(string gameDir, string name, string outDir)
    {
        var rpf = OpenGame(gameDir);
        var entry = rpf.AllRpfs.SelectMany(r => r.AllEntries ?? new List<RpfEntry>()).OfType<RpfFileEntry>().FirstOrDefault(e => e.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        if (entry == null)
        {
            Console.Error.WriteLine($"not found: {name}; similar:");
            string stem = Path.GetFileNameWithoutExtension(name);
            foreach (var e in rpf.AllRpfs.SelectMany(r => r.AllEntries ?? new List<RpfEntry>()).Where(e => e.Name.Contains(stem, StringComparison.OrdinalIgnoreCase)).Take(20))
                Console.Error.WriteLine("  " + e.Path);
            return 1;
        }
        Console.WriteLine($"found {entry.Path}");
        Directory.CreateDirectory(outDir);
        var data = entry.File.ExtractFile(entry);
        string xml = MetaXml.GetXml(entry, data, out string fname, outDir);
        File.WriteAllText(Path.Combine(outDir, fname), xml);
        Console.WriteLine($"wrote {fname}");
        return 0;
    }

    // print every text file whose path contains `pattern` (and ends with `ext`), up to `max`
    static int Cat(string gameDir, string pattern, string ext, int max)
    {
        var rpf = OpenGame(gameDir);
        int n = 0;
        foreach (var e in rpf.AllRpfs.SelectMany(r => r.AllEntries ?? new List<RpfEntry>()).OfType<RpfFileEntry>())
        {
            if (!e.Path.Contains(pattern, StringComparison.OrdinalIgnoreCase) || !e.Name.EndsWith(ext, StringComparison.OrdinalIgnoreCase)) continue;
            Console.WriteLine($"==== {e.Path}");
            var data = e.File.ExtractFile(e);
            Console.WriteLine(System.Text.Encoding.UTF8.GetString(data));
            if (++n >= max) break;
        }
        return 0;
    }

    static int Main(string[] args)
    {
        if (args.Length >= 2 && args[0] == "dlclist") return DlcList.Run(args[1], args.Length > 2 && args[2] == "--remove");
        if (args.Length >= 4 && args[0] == "build") return Build.Run(args[1], args[2], args[3]);
        if (args.Length >= 4 && args[0] == "verify") return Build.Verify(args[1], args[2], args[3]);
        if (args.Length >= 4 && args[0] == "cat") return Cat(args[1], args[2], args[3], args.Length > 4 ? int.Parse(args[4]) : 3);
        if (args.Length >= 4 && args[0] == "export") return Export(args[1], args[2], args[3]);
        Console.Error.WriteLine("usage: gtmpack export <gamedir> <file> <outdir>");
        return 2;
    }
}
