#include "test_helpers.hxx"

using namespace std;
using namespace pqxx;


// Test program for libpqxx.  Verify abort behaviour of RobustTransaction.
//
// The program will attempt to add an entry to a table called "pqxxevents",
// with a key column called "year"--and then abort the change.
namespace
{

// Let's take a boring year that is not going to be in the "pqxxevents" table
const long BoringYear = 1977;


// Count events and specifically events occurring in Boring Year, leaving the
// former count in the result pair's first member, and the latter in second.
class CountEvents : public transactor<nontransaction>
{
  string m_table;
  pair<int, int> &m_results;
public:
  CountEvents(string Table, pair<int,int> &Results) :
    transactor<nontransaction>("CountEvents"),
    m_table(Table),
    m_results(Results)
  {
  }

  void operator()(argument_type &T)
  {
    const string CountQuery = "SELECT count(*) FROM " + m_table;
    row R;

    R = T.exec1(CountQuery);
    R.front().to(m_results.first);

    R = T.exec1(CountQuery + " WHERE year=" + to_string(BoringYear));
    R.front().to(m_results.second);
  }
};


struct deliberate_error : exception
{
};


class FailedInsert : public transactor<robusttransaction<serializable>>
{
  string m_table;
  static string LastReason;
public:
  FailedInsert(string Table) :
    transactor<argument_type>("FailedInsert018"),
    m_table(Table)
  {
  }

  void operator()(argument_type &T)
  {
    T.exec0(
	"INSERT INTO " + m_table + " VALUES (" +
	to_string(BoringYear) + ", '" + T.esc("yawn") + "')");

    throw deliberate_error();
  }

  void on_abort(const char Reason[]) noexcept
  {
    if (Reason != LastReason)
    {
      pqxx::test::expected_exception(
	"Transactor " + name() + " failed: " + Reason);
      LastReason = Reason;
    }
  }
};


string FailedInsert::LastReason;


void test_018(transaction_base &T)
{
  connection_base &C(T.conn());
  T.abort();

  {
    work T2(C);
    test::create_pqxxevents(T2);
    T2.commit();
  }

  const string Table = "pqxxevents";

  pair<int,int> Before;
  C.perform(CountEvents(Table, Before));
  PQXX_CHECK_EQUAL(
	Before.second,
	0,
	"Already have event for " + to_string(BoringYear) + ", cannot run.");

  const FailedInsert DoomedTransaction(Table);

  {
    quiet_errorhandler d(C);
    PQXX_CHECK_THROWS(
	C.perform(DoomedTransaction),
	deliberate_error,
	"Not getting expected exception from failing transactor.");
  }

  pair<int,int> After;
  C.perform(CountEvents(Table, After));

  PQXX_CHECK_EQUAL(After.first, Before.first, "Event count changed.");
  PQXX_CHECK_EQUAL(
	After.second,
	Before.second,
	"Event count for " + to_string(BoringYear) + " changed.");
}
} // namespace

PQXX_REGISTER_TEST_T(test_018, nontransaction)