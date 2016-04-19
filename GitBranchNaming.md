I'd suggest using separate branches and maintain at least 5 branches:<br />
1) Master (currently latest working(!) OMPT implementation) (none by now as still unfinished)<br />
2+3) Branches for the original Intel runtimes (e.g. for reference, merge checks, reverts...)<br />
4) OMPT with 13.x runtime (current active development as most advanced)<br />
5) OMPT with 14.x runtime (won't be stable as porting work starts, so it's good to still have a working reference)<br />


---

Reason for this is that this allows parallel development on everything while maintaining a stable version.

So corresponding names would be:
  * master
  * _date_-initial-intel-release or whatever appropriate
  * ompt-support-13x
  * ompt-support-14x


---

Note: This is still open for discussion but seems to be a good solution for now.