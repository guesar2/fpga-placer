import subprocess,pathlib,re,csv,time,statistics,json
import argparse
ap=argparse.ArgumentParser()
ap.add_argument('--benchmarks',type=pathlib.Path,required=True)
ap.add_argument('--output',type=pathlib.Path,default=pathlib.Path('best_comparison'))
ap.add_argument('--effort',default='0.005')
args=ap.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]; data=args.benchmarks.resolve(); dest=args.output.resolve(); dest.mkdir(parents=True,exist_ok=True)
rows=[]
with (dest/'results.csv').open('w') as f:
 writer=csv.DictWriter(f,fieldnames=['benchmark','seed','variant','hpwl','seconds','legal','restored']);writer.writeheader()
 for i in range(5):
  name=f'FPGA{i:02d}';p=data/name/name
  for seed in range(1,6):
   for variant,exe in [('original',root/'placer'),('best',root/'placer')]:
    stem=f'{name}_s{seed}_{variant}'; out=dest/(stem+'.pl')
    cmd=[str(exe),str(p)+'.sitemap',str(p)+'.nets',str(p)+'.pl',str(out),'--seed',str(seed),'--runs','1','--threads','1','--effort',args.effort,'--verbose']
    if variant=='best': cmd.append('--keep-best')
    start=time.monotonic();r=subprocess.run(cmd,capture_output=True,text=True,timeout=90);elapsed=time.monotonic()-start
    (dest/(stem+'.log')).write_text(r.stdout+r.stderr)
    assert r.returncode==0,(stem,r.stderr)
    hpwl=int(re.search(r'RESULT hpwl=(\d+)',r.stdout)[1])
    e=subprocess.run([str(data/'evaluator'),str(p)+'.sitemap',str(p)+'.nets',str(p)+'.pl',str(out)],capture_output=True,text=True,timeout=30)
    (dest/(stem+'.eval')).write_text(e.stdout+e.stderr)
    assert e.returncode==0 and 'integrity ...... PASS' in (e.stdout+e.stderr) and 'legality ...... PASS' in (e.stdout+e.stderr),stem
    assert int(re.search(r'Test Placement:\s+(\d+)',e.stdout+e.stderr)[1])==hpwl
    row=dict(benchmark=name,seed=seed,variant=variant,hpwl=hpwl,seconds=round(elapsed,4),legal=True,restored='restore best' in r.stderr)
    rows.append(row);writer.writerow(row);f.flush()
  a=[r for r in rows if r['benchmark']==name and r['variant']=='original'];b=[r for r in rows if r['benchmark']==name and r['variant']=='best']
  print(name,'original_mean',statistics.mean(r['hpwl'] for r in a),'best_mean',statistics.mean(r['hpwl'] for r in b),'restored',sum(r['restored'] for r in b),flush=True)
print('ALL',len(rows),'evaluations PASS',flush=True)
