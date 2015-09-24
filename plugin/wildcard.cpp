#include "7zip.h"

// original from http://www.codeproject.com/KB/string/WildMatch.aspx

bool wildcard_helper::test(UString &sString, UString &sWild)
{
    bool bAny = false;
    bool bNextIsOptional = false;
    bool bAutorizedChar = true;

    int i=0;
    int j=0;

    // Check all the string char by char

    while (i<sString.Length()) 
    {
        // Check index for array overflow
        if (j<sWild.Length())
        {
            // Manage '*' in the wildcard
            if (sWild[j]==L'*') 
            {
                // Go to next character in the wildcard
                j++;
                // End of the string and wildcard end 
                // with *, only test string validity
                if (j>=sWild.Length()) 
                {
                    // Check end of the string
                    return true;
                }
                bAny = true;
                bNextIsOptional = false;
            } 
            else 
            {
                // Optional char in the wildcard
                if (sWild[j]==L'^')
                {
                    // Go to next char in the wildcard and indicate 
                    // that the next is optional
                    j++;
                    bNextIsOptional = true;
                }
                else
                {
                    bAutorizedChar = true;

                    // IF :
                    bool curCharMatchWildcard = false;
                    if (// Current char match the wildcard
                        (curCharMatchWildcard = MyCharLower(sWild[j]) == MyCharLower(sString[i]))
                        // '?' is used and current char is in autorized char list
                        || (sWild[j] == L'?' && bAutorizedChar)
                        // Char is optional and it's not in the string
                        // and it's necessary to test if '*' make any 
                        // char browsing
                        || (bNextIsOptional && !(bAny && bAutorizedChar))) 
                    {
                        // If current char match wildcard, 
                        // we stop for any char browsing
                        if (curCharMatchWildcard)
                            bAny = false;

                        // If it's not an optional char who is not present,
                        // go to next
                        if (curCharMatchWildcard || sWild[j] == L'?')
                            i++;
                        j++;

                        bNextIsOptional = false;
                    } 
                    else if (bAny && bAutorizedChar) // If we are in any char browsing ('*')  // and curent char is autorized
                        // Go to next
                        i++;
                    else
                        return false;
                }
            }
        }
        else
        // End of the wildcard but not the 
        // end of the string => 
        // not matching
        return false;
    }

    if (j<sWild.Length() && sWild[j]==L'^')
    {
        bNextIsOptional = true;
        j++;
    }

    // If the string is shorter than wildcard 
    // we test end of the
    // wildcard to check matching
    while ((j<sWild.Length() && sWild[j]==L'*') || bNextIsOptional)
    {
        j++;
        bNextIsOptional = false;

        if (j<sWild.Length() && sWild[j]==L'^')
        {
            bNextIsOptional = true;
            j++;
        }
    }

    return j>=sWild.Length();
}